#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace continuum {
struct Vec3 {
    double x{}, y{}, z{};
    Vec3 operator+(Vec3 b) const { return {x+b.x, y+b.y, z+b.z}; }
    Vec3 operator-(Vec3 b) const { return {x-b.x, y-b.y, z-b.z}; }
    Vec3 operator*(double s) const { return {s*x, s*y, s*z}; }
};
inline double dot(Vec3 a, Vec3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}
inline double norm(Vec3 a) { return std::sqrt(dot(a, a)); }

enum class FaceClass { fast, corrected, invalid };
struct FaceGeometry {
    FaceClass kind = FaceClass::invalid;
    double alpha{}, beta{}, non_ortho{}, relative_skew{};
    Vec3 skew{}, correction{};
};

inline FaceGeometry geometry(
    Vec3 xp, Vec3 xn, Vec3 xf, Vec3 area,
    double h_ref, double skew_tol = 1e-12,
    double non_ortho_tol = 1e-12)
{
    FaceGeometry g;
    const Vec3 d = xn - xp;
    const double ds = dot(d, area);
    const double d2 = dot(d, d), s2 = dot(area, area);
    if (!(d2 > 0 && s2 > 0 && h_ref > 0)) return g;
    const double cosine = ds / std::sqrt(d2*s2);
    if (!std::isfinite(cosine) || cosine <= 1e-8) return g;

    // Intersect the centre line with the face plane.
    g.beta = dot(xf - xp, area) / ds;
    g.alpha = s2 / ds;
    g.skew = xf - (xp + d*g.beta);
    g.correction = area - d*g.alpha;
    g.relative_skew = norm(g.skew) / h_ref;
    g.non_ortho = norm(g.correction) / std::sqrt(s2);
    if (!std::isfinite(g.beta) || !std::isfinite(g.alpha) ||
        !std::isfinite(g.relative_skew) ||
        !std::isfinite(g.non_ortho)) return FaceGeometry{};

    g.kind = (g.relative_skew <= skew_tol &&
              g.non_ortho <= non_ortho_tol)
        ? FaceClass::fast : FaceClass::corrected;
    return g;
}
} // namespace continuum