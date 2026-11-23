// =====================================================================================
//  mt::WallTreatment.hpp -- Continuous All-y+ wall treatment (Menter / Kader).
//
//  The treatment is continuous across the viscous sublayer, the buffer layer and the
//  logarithmic layer, i.e. it is valid for 0.1 <= y+ <= 5000 without any singular
//  division at y_P -> 0 :
//
//    * friction velocity      : solved implicitly from the blended velocity profile
//                               u_blend(y+) * u_tau = u_parallel   (no 1/y_P)
//    * specific dissipation   : omega_P = sqrt(omega_vis^2 + omega_log^2)
//                               omega_vis = 6 nu / (beta1 y_P^2)
//                               omega_log = u_tau / (sqrt(beta*) kappa y_P)
//    * wall heat flux         : Kader temperature profile with a continuous
//                               conduction-resistance blend.
//
//  Language standard : C++20
// =====================================================================================
#pragma once

#include "mt/Types.hpp"

namespace mt {

struct WallTreatmentConfig {
    double kappa   = 0.41;   //!< von Karman constant
    double E       = 9.8;    //!< Reichardt smooth-wall constant
    double Bplus   = 5.5;    //!< log-law intercept
    double Pr      = 0.71;   //!< molecular Prandtl number
    double Prt     = 0.85;   //!< turbulent Prandtl number (<=0 -> Churchill-Ozoeweis)
    double betaStar = 0.09;  //!< beta* of the SST model
    double beta1   = 0.075;  //!< beta1 of the k-omega model
};

class AllYPlusWallTreatment {
public:
    explicit AllYPlusWallTreatment(const WallTreatmentConfig& cfg = {}) : cfg_(cfg) {}

    const WallTreatmentConfig& config() const { return cfg_; }

    // ---- geometry ----------------------------------------------------------------
    //!< Exact wall-normal distance  y_P = (x_P - x_f) . S_f / ||S_f||
    static double wallNormalDistance(const Vec3& xP, const Vec3& xf, const Vec3& Sf);

    // ---- velocity profile ---------------------------------------------------------
    //!< Reichardt-type smooth-wall profile in inner units
    double reichardtProfile(double yPlus) const;
    //!< Fully blended All-y+ profile (viscous sublayer <-> logarithmic layer)
    double blendedProfile(double yPlus) const;
    //!< Inverse of blendedProfile (used to build the wall distance map)
    double inverseProfile(double uPlus) const;

    // ---- wall friction ------------------------------------------------------------
    //!< Implicitly solve  u_blend(y+) u_tau = u_parallel  for u_tau  (no 1/y_P)
    double frictionVelocity(double yP, double uParallel, double nu) const;
    //!< Wall shear stress  tau_w = rho u_tau^2
    double wallShearStress(double uTau, double rho) const;
    //!< Friction coefficient  Cf = 2 (u_tau / u_parallel)^2
    double frictionCoefficient(double uTau, double uParallel) const;

    // ---- turbulence ---------------------------------------------------------------
    //!< Blended specific dissipation rate at the wall-adjacent cell
    double blendedOmega(double yP, double uTau, double nu) const;

    // ---- thermal -------------------------------------------------------------------
    //!< Kader temperature profile shape function Theta(y+)
    double kaderTheta(double yPlus) const;
    //!< Conduction resistance between the cell centre and the wall [m^2 K / W]
    double conductionResistance(double yP, double uTau, double nu, double rho, double cp) const;
    //!< Wall heat flux [W/m^2]  (positive: into the fluid)
    double wallHeatFlux(double yP, double TP, double Tw, double uTau, double nu, double rho, double cp) const;
    //!< Turbulent Prandtl number (Churchill-Ozoeweis correlation)
    double turbulentPrandtl() const;

private:
    WallTreatmentConfig cfg_;
};

} // namespace mt
