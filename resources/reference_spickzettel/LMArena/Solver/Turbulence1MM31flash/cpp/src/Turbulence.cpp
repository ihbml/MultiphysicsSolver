// =====================================================================================
//  mt::Turbulence.cpp -- SST (Menter 2003), Spalart-Allmaras (2012) and WALE closures.
// =====================================================================================
#include "mt/Turbulence.hpp"

#include <algorithm>
#include <cmath>

namespace mt {

const char* turbModelName(TurbModel m) {
    switch (m) {
        case TurbModel::SST:  return "SST k-omega (Menter 2003)";
        case TurbModel::SA:   return "Spalart-Allmaras (2012 vane form)";
        case TurbModel::WALE: return "WALE LES sub-grid scale model";
        default:              return "laminar (no turbulence model)";
    }
}

TurbulenceModel::TurbulenceModel(const Mesh& mesh, const TurbulenceConfig& cfg)
    : mesh_(mesh), cfg_(cfg), nCells_(mesh.nCells) {
    const std::size_t n = static_cast<std::size_t>(std::max(nCells_, 0));
    nut_.assign(n, 0.0);
    production_.assign(n, 0.0);
    sigmaK_.assign(n, cfg_.sigmaK2);
    sigmaW_.assign(n, cfg_.sigmaW2);
    beta_.assign(n, cfg_.beta2);
    gamma_.assign(n, cfg_.gamma2);
    crossDiffusion_.assign(n, 0.0);
    f1_.assign(n, 0.0);
    f2_.assign(n, 0.0);
    saStilde_.assign(n, 0.0);
    saFw_.assign(n, 0.0);
    limiterActive_.assign(n, 0);
}

// -------------------------------------------------------------------------------------
//  Blending functions (Menter 2003, wall-distance formulation)
// -------------------------------------------------------------------------------------
double TurbulenceModel::computeF1(int cell, double k, double omega, double crossDiffusion) const {
    const double y = mesh_.wallDistanceOf(cell);
    if (y <= 1e-12 || omega <= 1e-12) return 1.0;
    const double kk = std::max(k, 0.0);
    const double argA = std::sqrt(kk) / (cfg_.betaStar * omega * y);
    const double argB = 500.0 * cfg_.nu / (y * y * omega);
    const double argMax = std::max(argA, argB);
    const double cd = std::max(crossDiffusion, 1.0e-10);
    const double argC = 4.0 * cfg_.rho * cfg_.sigmaW2 * kk / (cd * y * y);
    double arg1 = std::min(argMax, argC);
    if (!(arg1 > 0.0)) arg1 = 0.0;
    const double a2 = arg1 * arg1;
    return std::tanh(a2 * a2);
}

double TurbulenceModel::computeF2(int cell, double k, double omega) const {
    const double y = mesh_.wallDistanceOf(cell);
    if (y <= 1e-12 || omega <= 1e-12) return 0.0;
    const double argA = 2.0 * std::sqrt(std::max(k, 0.0)) / (cfg_.betaStar * omega * y);
    const double argB = 500.0 * cfg_.nu / (y * y * omega);
    const double arg2 = std::max(argA, argB);
    return std::tanh(arg2 * arg2);
}

double TurbulenceModel::bradshawLimitedNut(double k, double omega, double S, double F2) const {
    const double num = cfg_.a1 * std::max(k, 0.0);
    const double den = std::max(cfg_.a1 * std::max(omega, 0.0), S * F2);
    return den > kSmall ? num / den : 0.0;
}

// -------------------------------------------------------------------------------------
//  Strain / rotation helpers
// -------------------------------------------------------------------------------------
SymMat3 TurbulenceModel::deviatoricStrain(const Mat3& G) {
    const double div = G(0, 0) + G(1, 1) + G(2, 2);
    SymMat3 S;
    S.xx = 0.5 * (G(0, 0) + G(0, 0)) - div / 3.0;
    S.yy = 0.5 * (G(1, 1) + G(1, 1)) - div / 3.0;
    S.zz = 0.5 * (G(2, 2) + G(2, 2)) - div / 3.0;
    S.xy = 0.5 * (G(0, 1) + G(1, 0));
    S.yz = 0.5 * (G(1, 2) + G(2, 1));
    S.zx = 0.5 * (G(2, 0) + G(0, 2));
    return S;
}

SymMat3 TurbulenceModel::deviatoricStrain(const std::vector<Mat3>& gradU, int cell) {
    return deviatoricStrain(gradU[static_cast<std::size_t>(cell)]);
}

double TurbulenceModel::rotationRate(const Mat3& G) {
    const double wxy = 0.5 * (G(0, 1) - G(1, 0));
    const double wyz = 0.5 * (G(1, 2) - G(2, 1));
    const double wzx = 0.5 * (G(2, 0) - G(0, 2));
    return std::sqrt(2.0 * (wxy * wxy + wyz * wyz + wzx * wzx));
}

// -------------------------------------------------------------------------------------
//  WALE
// -------------------------------------------------------------------------------------
double TurbulenceModel::waleSgs(double delta, const SymMat3& Sd, const SymMat3& Sbar) const {
    const double sd2 = Sd.xx * Sd.xx + Sd.yy * Sd.yy + Sd.zz * Sd.zz
                     + 2.0 * (Sd.xy * Sd.xy + Sd.yz * Sd.yz + Sd.zx * Sd.zx);
    const double sb2 = Sbar.xx * Sbar.xx + Sbar.yy * Sbar.yy + Sbar.zz * Sbar.zz
                     + 2.0 * (Sbar.xy * Sbar.xy + Sbar.yz * Sbar.yz + Sbar.zx * Sbar.zx);
    const double cw = cfg_.waleCw;
    const double num = std::pow(sd2, 1.5);
    const double den = std::pow(sb2, 2.5) + std::pow(sd2, 1.25);
    if (den < kSmall) return 0.0;
    return (cw * delta) * (cw * delta) * num / den;
}

SymMat3 TurbulenceModel::filteredStrain(int cell, const std::vector<SymMat3>& SdCells) const {
    // Discrete top-hat filter: weighted average of the deviatoric strain rate over the
    // cell and its face neighbours (weights = 1/d).  This is the discrete equivalent of
    // the Favre-averaged filtered strain rate entering the WALE denominator.
    SymMat3 acc{};
    double wsum = 0.0;
    const int e0 = mesh_.cellFaces.begin(cell), e1 = mesh_.cellFaces.end(cell);
    for (int e = e0; e < e1; ++e) {
        const int f = mesh_.cellFaces.faceIndex[static_cast<std::size_t>(e)];
        const int nb = mesh_.faces.neighbour[static_cast<std::size_t>(f)];
        if (nb < 0) continue;
        const double w = mesh_.cellFaces.weight[static_cast<std::size_t>(e)];
        const SymMat3& S = SdCells[static_cast<std::size_t>(nb)];
        acc.xx += w * S.xx; acc.yy += w * S.yy; acc.zz += w * S.zz;
        acc.xy += w * S.xy; acc.yz += w * S.yz; acc.zx += w * S.zx;
        wsum += w;
    }
    if (wsum < kSmall) return SdCells[static_cast<std::size_t>(cell)];
    acc.xx /= wsum; acc.yy /= wsum; acc.zz /= wsum;
    acc.xy /= wsum; acc.yz /= wsum; acc.zx /= wsum;
    return acc;
}

// -------------------------------------------------------------------------------------
//  update()
// -------------------------------------------------------------------------------------
void TurbulenceModel::update(const std::vector<Mat3>& gradU,
                             const std::vector<double>& divU,
                             const std::vector<double>& k,
                             const std::vector<double>& omega,
                             const std::vector<double>& nuTilde,
                             const std::vector<Vec3>* gradK,
                             const std::vector<Vec3>* gradW) {
    const bool isSST  = (cfg_.model == TurbModel::SST);
    const bool isSA   = (cfg_.model == TurbModel::SA);
    const bool isWALE = (cfg_.model == TurbModel::WALE);

    // ---- WALE needs the filtered strain rate: pre-compute the deviatoric strains ----
    std::vector<SymMat3> sdCells;
    if (isWALE) {
        sdCells.resize(static_cast<std::size_t>(nCells_));
        for (int c = 0; c < nCells_; ++c) sdCells[static_cast<std::size_t>(c)] = deviatoricStrain(gradU[static_cast<std::size_t>(c)]);
    }

    for (int c = 0; c < nCells_; ++c) {
        const Mat3& G = gradU[static_cast<std::size_t>(c)];
        const SymMat3 Sd = deviatoricStrain(G);
        const double sd2 = Sd.xx * Sd.xx + Sd.yy * Sd.yy + Sd.zz * Sd.zz
                         + 2.0 * (Sd.xy * Sd.xy + Sd.yz * Sd.yz + Sd.zx * Sd.zx);
        const double S = std::sqrt(2.0 * std::max(sd2, 0.0));
        const double Omega = rotationRate(G);
        const double kk = k.empty() ? 0.0 : std::max(k[static_cast<std::size_t>(c)], 0.0);
        const double ww = omega.empty() ? 0.0 : std::max(omega[static_cast<std::size_t>(c)], 0.0);

        if (isSST) {
            // ---- cross diffusion magnitude CD_kw (used by F1) -----------------------
            double cd = 1.0e-10;
            double gradKdotGradW = 0.0;
            if (gradK && gradW) {
                gradKdotGradW = dot((*gradK)[static_cast<std::size_t>(c)], (*gradW)[static_cast<std::size_t>(c)]);
                cd = std::max(2.0 * cfg_.rho * cfg_.sigmaW2 * gradKdotGradW / std::max(ww, 1.0e-12), 1.0e-10);
            }
            const double F1 = computeF1(c, kk, ww, cd);
            const double F2 = computeF2(c, kk, ww);
            f1_[static_cast<std::size_t>(c)] = F1;
            f2_[static_cast<std::size_t>(c)] = F2;

            // ---- Bradshaw limited eddy viscosity -----------------------------------
            const double nut = bradshawLimitedNut(kk, ww, S, F2);
            nut_[static_cast<std::size_t>(c)] = nut;

            // ---- blended transport coefficients ------------------------------------
            sigmaK_[static_cast<std::size_t>(c)] = F1 * cfg_.sigmaK1 + (1.0 - F1) * cfg_.sigmaK2;
            sigmaW_[static_cast<std::size_t>(c)] = F1 * cfg_.sigmaW1 + (1.0 - F1) * cfg_.sigmaW2;
            beta_  [static_cast<std::size_t>(c)] = F1 * cfg_.beta1  + (1.0 - F1) * cfg_.beta2;
            gamma_ [static_cast<std::size_t>(c)] = F1 * cfg_.gamma1 + (1.0 - F1) * cfg_.gamma2;

            // ---- production with the Menter limiter ---------------------------------
            //  P_k = tau_ij du_i/dx_j = 2 rho nu_t S_ij S_ij      (incompressible)
            double Pk = 2.0 * cfg_.rho * nut * sd2;
            const double PkLimit = cfg_.productionLimiter * cfg_.betaStar * cfg_.rho * kk * ww;
            if (Pk > PkLimit) { Pk = PkLimit; limiterActive_[static_cast<std::size_t>(c)] = 1; }
            else limiterActive_[static_cast<std::size_t>(c)] = 0;
            production_[static_cast<std::size_t>(c)] = std::max(Pk, 0.0);

            // ---- cross diffusion source of the omega equation -----------------------
            //  D_w = 2 (1 - F1) rho sigma_w2 (1/omega) grad k . grad w
            crossDiffusion_[static_cast<std::size_t>(c)] =
                2.0 * (1.0 - F1) * cfg_.rho * cfg_.sigmaW2 * gradKdotGradW / std::max(ww, 1.0e-12);
        } else if (isSA) {
            const double nutilde = nuTilde.empty() ? 0.0 : std::max(nuTilde[static_cast<std::size_t>(c)], 0.0);
            const double chi = nutilde / std::max(cfg_.nu, 1e-30);
            const double chi3 = chi * chi * chi;
            const double fv1 = chi3 / (chi3 + cfg_.saCv1 * cfg_.saCv1 * cfg_.saCv1);
            const double fv2 = 1.0 - chi / (1.0 + chi * fv1);
            const double d = std::max(mesh_.wallDistanceOf(c), 1e-12);
            const double kk2 = cfg_.kappa * cfg_.kappa * d * d;
            const double Stilde = std::max(Omega + fv2 * nutilde / kk2, cfg_.saCs * Omega);
            const double r = std::min(nutilde / std::max(Stilde * kk2, 1e-30), 10.0);
            const double r6 = std::pow(r, 6.0);
            const double g = r + cfg_.saCw2 * (r6 - r);
            const double cw36 = std::pow(cfg_.saCw3, 6.0);
            const double fw = g * std::pow((1.0 + cw36) / (std::pow(g, 6.0) + cw36), 1.0 / 6.0);

            saStilde_[static_cast<std::size_t>(c)] = Stilde;
            saFw_[static_cast<std::size_t>(c)] = fw;
            nut_[static_cast<std::size_t>(c)] = nutilde * fv1;
            production_[static_cast<std::size_t>(c)] = cfg_.saCb1 * Stilde * nutilde * cfg_.rho;
            limiterActive_[static_cast<std::size_t>(c)] = 0;
            crossDiffusion_[static_cast<std::size_t>(c)] = 0.0;
        } else if (isWALE) {
            const double delta = filterWidth(c);
            const SymMat3 Sbar = filteredStrain(c, sdCells);
            nut_[static_cast<std::size_t>(c)] = waleSgs(delta, Sd, Sbar);
            production_[static_cast<std::size_t>(c)] = 0.0;
            limiterActive_[static_cast<std::size_t>(c)] = 0;
            crossDiffusion_[static_cast<std::size_t>(c)] = 0.0;
        } else {
            nut_[static_cast<std::size_t>(c)] = 0.0;
            production_[static_cast<std::size_t>(c)] = 0.0;
            limiterActive_[static_cast<std::size_t>(c)] = 0;
            crossDiffusion_[static_cast<std::size_t>(c)] = 0.0;
        }
    }
}

double TurbulenceModel::maxCrossDiffusion() const {
    double m = 0.0;
    for (double v : crossDiffusion_) m = std::max(m, std::abs(v));
    return m;
}

TurbulenceModel::Diagnostics TurbulenceModel::diagnostics(const std::vector<double>& k,
                                                           const std::vector<double>& omega) const {
    Diagnostics d;
    d.minK = 1e300; d.maxK = -1e300; d.minOmega = 1e300; d.maxOmega = -1e300;
    for (std::size_t i = 0; i < k.size(); ++i) {
        d.minK = std::min(d.minK, k[i]); d.maxK = std::max(d.maxK, k[i]);
    }
    for (std::size_t i = 0; i < omega.size(); ++i) {
        d.minOmega = std::min(d.minOmega, omega[i]); d.maxOmega = std::max(d.maxOmega, omega[i]);
    }
    for (char c : limiterActive_) if (c) ++d.limiterCells;
    d.maxCrossDiffusion = maxCrossDiffusion();
    return d;
}

} // namespace mt
