#pragma once
#include "face_geometry.hpp"
#include <algorithm>
#include <stdexcept>

namespace continuum {
struct FaceCoefficients { double pp, pn, np, nn; };
inline FaceCoefficients coefficients(
    const FaceGeometry& g, double mdot, double gamma, bool upwind)
{
    if (g.kind == FaceClass::invalid || gamma < 0)
        throw std::invalid_argument("Invalid face or diffusion coefficient");
    const double D = gamma*g.alpha;
    const double p = upwind ? std::max(mdot, 0.0) + D
                            : mdot*(1-g.beta) + D;
    const double n = upwind ? std::min(mdot, 0.0) - D
                            : mdot*g.beta - D;
    return {p, n, -p, -n};
}

inline double central_flux(
    const FaceGeometry& g, double mdot, double gamma,
    double phi_p, double phi_n, Vec3 face_gradient,
    double omega = 1.0)
{
    const auto a = coefficients(g, mdot, gamma, false);
    const double correction = mdot*dot(face_gradient, g.skew)
                            - gamma*dot(face_gradient, g.correction);
    return a.pp*phi_p + a.pn*phi_n + omega*correction;
}
} // namespace continuum