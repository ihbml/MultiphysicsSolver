// =====================================================================================
//  mt::WallTreatment.cpp -- continuous All-y+ wall treatment (Menter / Kader)
// =====================================================================================
#include "mt/WallTreatment.hpp"

#include <algorithm>
#include <cmath>

namespace mt {

// -------------------------------------------------------------------------------------
//  Geometry
// -------------------------------------------------------------------------------------
double AllYPlusWallTreatment::wallNormalDistance(const Vec3& xP, const Vec3& xf, const Vec3& Sf) {
    const double a = mag(Sf);
    if (a < kSmall) return 0.0;
    return dot(xP - xf, Sf) / a;
}

// -------------------------------------------------------------------------------------
//  Velocity profiles
// -------------------------------------------------------------------------------------
double AllYPlusWallTreatment::reichardtProfile(double yPlus) const {
    if (yPlus <= 1.0) return yPlus;
    // Reichardt (1951) smooth-wall profile
    return 2.5 * std::log(yPlus) + cfg_.Bplus
         - 0.5 * std::exp(-0.406 * std::pow(yPlus, 0.85));
}

double AllYPlusWallTreatment::blendedProfile(double yPlus) const {
    if (yPlus <= 0.0) return 0.0;
    const double visc = yPlus;                              // viscous sublayer : u+ = y+
    const double log  = reichardtProfile(yPlus);           // buffer + log layer
    const double phi  = smoothStep(2.0, 20.0, yPlus);      // continuous blending
    return (1.0 - phi) * visc + phi * log;
}

double AllYPlusWallTreatment::inverseProfile(double uPlus) const {
    if (uPlus <= 0.0) return 0.0;
    double lo = 0.0, hi = 1.0;
    while (blendedProfile(hi) < uPlus && hi < 1.0e8) hi *= 2.0;
    for (int it = 0; it < 100; ++it) {
        const double mid = 0.5 * (lo + hi);
        if (blendedProfile(mid) < uPlus) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

// -------------------------------------------------------------------------------------
//  Friction velocity : implicit solve, no division by y_P
// -------------------------------------------------------------------------------------
double AllYPlusWallTreatment::frictionVelocity(double yP, double uParallel, double nu) const {
    const double u = std::abs(uParallel);
    if (u < 1.0e-14) return 0.0;
    if (yP <= 1.0e-14 || nu <= 1.0e-30) return 0.0;

    const auto residual = [&](double ut) {
        const double yPlus = yP * ut / nu;
        return blendedProfile(yPlus) * ut - u;
    };

    double lo = 0.0;
    double hi = std::sqrt(u * nu / yP);          // viscous sublayer estimate
    if (hi <= 0.0) hi = u;
    int guard = 0;
    while (residual(hi) < 0.0 && guard++ < 80) hi *= 2.0;

    for (int it = 0; it < 90; ++it) {
        const double mid = 0.5 * (lo + hi);
        if (residual(mid) > 0.0) hi = mid; else lo = mid;
    }
    double ut = 0.5 * (lo + hi);

    // Newton polishing on the continuous profile
    for (int it = 0; it < 6; ++it) {
        const double yPlus = yP * ut / nu;
        const double h = 1.0e-6 * std::max(yPlus, 1.0);
        const double dp = (blendedProfile(yPlus + h) - blendedProfile(yPlus - h)) / (2.0 * h);
        const double g = blendedProfile(yPlus) * ut - u;
        const double dg = dp * ut + blendedProfile(yPlus);
        if (std::abs(dg) < 1.0e-300) break;
        const double next = ut - g / dg;
        if (!(next > 0.0)) break;
        ut = next;
    }
    return ut;
}

double AllYPlusWallTreatment::wallShearStress(double uTau, double rho) const {
    return rho * uTau * uTau;
}

double AllYPlusWallTreatment::frictionCoefficient(double uTau, double uParallel) const {
    const double u = std::abs(uParallel);
    if (u < 1.0e-14) return 0.0;
    return 2.0 * (uTau / u) * (uTau / u);   // Cf = tau_w / (0.5 rho u^2)
}

// -------------------------------------------------------------------------------------
//  Turbulence : blended specific dissipation rate at the wall
//     omega_vis = 6 nu / (beta1 y_P^2)
//     omega_log = u_tau / (sqrt(beta*) kappa y_P)
//     omega_P   = sqrt(omega_vis^2 + omega_log^2)
// -------------------------------------------------------------------------------------
double AllYPlusWallTreatment::blendedOmega(double yP, double uTau, double nu) const {
    if (yP <= 1.0e-14) return 1.0e30;
    const double omegaVis = 6.0 * nu / (cfg_.beta1 * yP * yP);
    const double omegaLog = uTau / (std::sqrt(cfg_.betaStar) * cfg_.kappa * yP);
    return std::sqrt(omegaVis * omegaVis + omegaLog * omegaLog);
}

// -------------------------------------------------------------------------------------
//  Thermal : Kader temperature profile
// -------------------------------------------------------------------------------------
double AllYPlusWallTreatment::turbulentPrandtl() const {
    if (cfg_.Prt > 0.0) return cfg_.Prt;
    const double Pr = cfg_.Pr;
    if (Pr <= 0.5) return 0.85;
    // Churchill-Ozoeweis correlation
    return 0.5 + std::sqrt(Pr) * std::log((5.0 * Pr + 1.0) / 6.0) / std::log(Pr);
}

double AllYPlusWallTreatment::kaderTheta(double yPlus) const {
    const double Bt = -0.17 + 1.24 * std::pow(cfg_.Pr, 0.43);   // Kader intercept
    const double thLog  = (1.0 / cfg_.kappa) * std::log(std::max(yPlus, 1.0)) + Bt;
    const double thVisc = cfg_.Pr * yPlus / cfg_.kappa;          // conduction resistance form
    const double phi = smoothStep(5.0, 30.0, yPlus);
    return (1.0 - phi) * thVisc + phi * thLog;
}

double AllYPlusWallTreatment::conductionResistance(double yP, double uTau, double nu,
                                                   double rho, double cp) const {
    if (uTau <= 0.0 || rho <= 0.0 || cp <= 0.0) return 0.0;
    const double yPlus = std::max(yP * uTau / nu, 1.0e-12);
    const double Bt = -0.17 + 1.24 * std::pow(cfg_.Pr, 0.43);
    const double Rlog  = (std::log(std::max(yPlus, 1.0)) + cfg_.kappa * Bt) / (rho * cp * uTau);
    const double Rvisc = cfg_.Pr * yPlus / (rho * cp * uTau);
    const double phi = smoothStep(5.0, 30.0, yPlus);
    return (1.0 - phi) * Rvisc + phi * Rlog;
}

double AllYPlusWallTreatment::wallHeatFlux(double yP, double TP, double Tw, double uTau,
                                           double nu, double rho, double cp) const {
    const double R = conductionResistance(yP, uTau, nu, rho, cp);
    if (R <= 0.0) return 0.0;
    return (TP - Tw) / R;
}

} // namespace mt
