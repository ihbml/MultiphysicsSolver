// =====================================================================================
//  mt::Turbulence.hpp -- Industrial RANS / LES turbulence closures for polyhedral FVM.
//
//  * SST k-omega (Menter 2003) : F1/F2 blending, cross-diffusion, Bradshaw limited
//                                 eddy viscosity, Patankar positivity-preserving
//                                 source linearisation, production limiter.
//  * Spalart-Allmaras (2012 vane form) : one-equation nu-tilde with stress limiter.
//  * WALE (LES sub-grid scale) : algebraic Smagorinsky-like model on the traceless
//                                 squared velocity gradient tensor.
//
//  Language standard : C++20
// =====================================================================================
#pragma once

#include "mt/Mesh.hpp"
#include "mt/Types.hpp"

#include <string>
#include <vector>

namespace mt {

enum class TurbModel : int { None = 0, SST = 1, SA = 2, WALE = 3 };

const char* turbModelName(TurbModel m);

struct TurbulenceConfig {
    TurbModel model = TurbModel::SST;

    // ---- free-stream values ------------------------------------------------------
    double kInf     = 1.0;      //!< free-stream turbulent kinetic energy      [m^2/s^2]
    double omegaInf = 1.0;      //!< free-stream specific dissipation rate    [1/s]

    // ---- SST k-omega (Menter 2003) ------------------------------------------------
    double a1        = 0.31;    //!< Bradshaw coefficient  (nu_t = a1 k / max(a1 w, S F2))
    double betaStar  = 0.09;    //!< beta*                                  [-]
    double sigmaK1   = 0.85;    //!< turbulent Prandtl for k (inner)          [-]
    double sigmaK2   = 1.00;    //!< turbulent Prandtl for k (outer)          [-]
    double sigmaW1   = 0.50;    //!< turbulent Prandtl for omega (inner)      [-]
    double sigmaW2   = 0.856;   //!< turbulent Prandtl for omega (outer)      [-]
    double beta1     = 0.075;   //!< destruction coefficient (inner)          [-]
    double beta2     = 0.0828;  //!< destruction coefficient (outer)          [-]
    double gamma1    = 0.553166667;  //!< gamma (inner)                      [-]
    double gamma2    = 0.440354667;  //!< gamma (outer)                      [-]
    double productionLimiter = 10.0; //!< P_k <= limiter * beta* rho k omega

    // ---- Spalart-Allmaras (2012 vane form) ---------------------------------------
    double saCb1  = 0.1355;     //!< production coefficient                   [-]
    double saCb2  = 0.622;      //!< cross-diffusion coefficient              [-]
    double saCv1  = 7.1;        //!< viscosity function coefficient           [-]
    double saCs   = 0.3;        //!< rotation limiter                         [-]
    double saCw2  = 0.3;        //!< wall destruction coefficient             [-]
    double saCw3  = 2.0;        //!< wall destruction coefficient             [-]
    double saSigma = 2.0 / 3.0; //!< turbulent Prandtl for nu-tilde           [-]

    // ---- WALE --------------------------------------------------------------------
    double waleCw = 0.5;        //!< WALE coefficient                         [-]

    // ---- universal ---------------------------------------------------------------
    double kappa = 0.41;        //!< von Karman constant                      [-]
    double nu    = 1.5e-5;      //!< kinematic viscosity                      [m^2/s]
    double rho   = 1.225;       //!< density                                  [kg/m^3]
};

// -------------------------------------------------------------------------------------
//  TurbulenceModel : the closure.  The solver owns the transported fields, the model
//  owns the closure coefficients and the diagnostic quantities.
// -------------------------------------------------------------------------------------
class TurbulenceModel {
public:
    TurbulenceModel(const Mesh& mesh, const TurbulenceConfig& cfg);

    TurbModel id() const { return cfg_.model; }
    const char* name() const { return turbModelName(cfg_.model); }
    int  nCells() const { return nCells_; }

    //!< Refresh all closure quantities from the current fields.
    //!< gradU holds the full velocity gradient tensor G_ij = d u_i / d x_j.
    void update(const std::vector<Mat3>& gradU,
                const std::vector<double>& divU,
                const std::vector<double>& k,
                const std::vector<double>& omega,
                const std::vector<double>& nuTilde,
                const std::vector<Vec3>* gradK = nullptr,
                const std::vector<Vec3>* gradW = nullptr);

    //!< Spalart-Allmaras auxiliary quantities (exposed for the solver assembly)
    const std::vector<double>& saStilde() const { return saStilde_; }
    const std::vector<double>& saFw()     const { return saFw_; }
    double saCw1() const {
        return cfg_.saCb1 / (cfg_.kappa * cfg_.kappa) + (1.0 + cfg_.saCb2) / cfg_.saSigma;
    }

    const std::vector<double>& nut()             const { return nut_; }
    const std::vector<double>& production()      const { return production_; }
    const std::vector<double>& sigmaK()          const { return sigmaK_; }
    const std::vector<double>& sigmaW()          const { return sigmaW_; }
    const std::vector<double>& beta()            const { return beta_; }
    const std::vector<double>& gamma()           const { return gamma_; }
    const std::vector<double>& crossDiffusion()  const { return crossDiffusion_; }
    const std::vector<double>& blendingF1()      const { return f1_; }
    const std::vector<double>& blendingF2()      const { return f2_; }
    bool limiterActive(int cell) const { return limiterActive_[static_cast<std::size_t>(cell)] != 0; }
    double maxCrossDiffusion() const;

    // ---- SST blending functions (exposed for unit verification) -------------------
    double computeF1(int cell, double k, double omega, double crossDiffusion) const;
    double computeF2(int cell, double k, double omega) const;
    //!< Bradshaw-limited eddy viscosity:  nu_t = a1 k / max(a1 omega, S F2)
    double bradshawLimitedNut(double k, double omega, double S, double F2) const;

    // ---- WALE ---------------------------------------------------------------------
    //!< traceless (deviatoric) strain-rate tensor of the cell
    static SymMat3 deviatoricStrain(const std::vector<Mat3>& gradU, int cell);
    //!< rotation-rate tensor component norm:  Omega = sqrt(2 Omega_ij Omega_ij)
    static double rotationRate(const Mat3& G);
    //!< nu_sgs = (Cw Delta)^2 (Sd Sd)^{3/2} / ((Sbar Sbar)^{5/2} + (Sd Sd)^{5/4})
    double waleSgs(double delta, const SymMat3& Sd, const SymMat3& Sbar) const;
    //!< filter width of the cell (equivalent cube edge)
    double filterWidth(int cell) const { return mesh_.cellSize(cell); }
    //!< filtered strain rate (discrete top-hat filter over the cell neighbourhood)
    SymMat3 filteredStrain(int cell, const std::vector<SymMat3>& SdCells) const;

    //!< deviatoric strain-rate tensor of the cell from the full gradient tensor
    static SymMat3 deviatoricStrain(const Mat3& G);

    // ---- diagnostics --------------------------------------------------------------
    struct Diagnostics {
        double minK = 0.0, maxK = 0.0;
        double minOmega = 0.0, maxOmega = 0.0;
        int    limiterCells = 0;
        double maxCrossDiffusion = 0.0;
    };
    Diagnostics diagnostics(const std::vector<double>& k, const std::vector<double>& omega) const;

private:
    const Mesh&         mesh_;
    TurbulenceConfig    cfg_;
    int                 nCells_ = 0;
    std::vector<double> nut_, production_, sigmaK_, sigmaW_, beta_, gamma_;
    std::vector<double> crossDiffusion_, f1_, f2_;
    std::vector<double> saStilde_, saFw_;
    std::vector<char>   limiterActive_;
};

} // namespace mt
