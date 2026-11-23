// =====================================================================================
//  mt::Solver.hpp -- segregated SIMPLE / PISO solver with Rhie-Chow interpolation.
//
//  * incompressible / weakly compressible, steady or transient
//  * cell-centred unstructured polyhedral finite volume
//  * upwind convection, second-order (deferred) corrected diffusion with the
//    CorrectionSoA non-orthogonal correction
//  * Patankar positivity-preserving source linearisation for k, omega and nu-tilde
//  * multi-region conjugate heat transfer (monolithic or partitioned)
//  * anisotropic Darcy-Forchheimer porous zones
//  * All-y+ wall treatment (wall functions) or resolved wall stress
//
//  Language standard : C++20
// =====================================================================================
#pragma once

#include "mt/Mesh.hpp"
#include "mt/Physics.hpp"
#include "mt/Turbulence.hpp"
#include "mt/WallTreatment.hpp"

#include <vector>

namespace mt {

struct SolverConfig {
    // ---- time integration ---------------------------------------------------------
    bool   transient = false;
    double timeStep = 1.0e-4;
    double pseudoTimeStep = 1.0;    //!< pseudo time step of the steady solver
    int    maxIterations = 2000;
    double tolerance = 1.0e-8;

    // ---- relaxation ---------------------------------------------------------------
    double underRelaxU = 0.7;
    double underRelaxK = 0.5;
    double underRelaxW = 0.5;
    double underRelaxP = 0.3;
    double underRelaxT = 0.9;
    int    pisoIterations = 1;
    bool   rhieChow = true;

    // ---- fluid properties ---------------------------------------------------------
    double rho = 1.225;
    double mu = 1.81e-5;
    double cp = 1005.0;
    double Pr = 0.71;
    double Prt = 0.85;

    // ---- equation controls --------------------------------------------------------
    bool   solveEnergy = true;
    bool   solvePressure = true;
    bool   prescribedPressureGradient = false;
    Vec3   pressureGradient{0.0, 0.0, 0.0};   //!< grad p (not the body force)

    // ---- turbulence ----------------------------------------------------------------
    TurbModel turbulence = TurbModel::SST;
    TurbulenceConfig turb;
    WallTreatmentConfig wall;

    // ---- wall treatment ------------------------------------------------------------
    bool   wallFunctions = true;   //!< true : All-y+ wall functions, false : resolved wall
    double wallHeatFluxBC = 0.0;   //!< default prescribed wall heat flux [W/m^2]

    // ---- CHT -----------------------------------------------------------------------
    std::vector<CHTRegionProperties> regions;
    CHTCoupling cht;
    bool   chtMonolithic = true;
    double chtRelaxation = 0.6;

    // ---- porous media --------------------------------------------------------------
    PorousModule porous;
};

struct SolverDiagnostics {
    int    iterations = 0;
    bool   converged = false;
    double residualU = 0.0;
    double residualP = 0.0;
    double residualT = 0.0;
    double residualK = 0.0;
    double residualOmega = 0.0;
    double minK = 0.0;
    double minOmega = 0.0;
    int    limiterCells = 0;
    std::vector<double> historyU, historyP, historyT, historyK, historyOmega;
};

class Solver {
public:
    Solver(Mesh& mesh, const SolverConfig& cfg);

    void initialise();
    //!< run the steady (pseudo-transient SIMPLE) loop
    SolverDiagnostics runSteady();
    //!< advance one transient time step (PISO)
    SolverDiagnostics advance();

    // ---- solution fields ------------------------------------------------------------
    std::vector<double> p, T, k, omega, nuTilde;
    std::vector<Vec3>   U;
    std::vector<double> massFlux;      //!< per face
    std::vector<double> wallShear;     //!< per wall face [Pa]
    std::vector<double> wallHeatFlux;  //!< per wall face [W/m^2]
    std::vector<double> wallUtau;      //!< per wall face friction velocity [m/s]
    std::vector<double> interfaceTemperature;  //!< partitioned CHT interface values

    TurbulenceModel& turbulenceModel() { return turb_; }
    const TurbulenceModel& turbulenceModel() const { return turb_; }
    const SolverDiagnostics& diagnostics() const { return diag_; }
    const SolverConfig& config() const { return cfg_; }
    double wallShearTotal() const { return wallShearTotal_; }

private:
    Mesh&        mesh_;
    SolverConfig cfg_;
    TurbulenceModel turb_;
    AllYPlusWallTreatment wall_;
    SolverDiagnostics diag_;

    std::vector<Vec3>   Uold_;
    std::vector<Mat3>   gradUTensor_;
    std::vector<double> divU_;
    std::vector<Vec3>   gradT_, gradK_, gradW_;
    std::vector<double> nut_;
    double wallShearTotal_ = 0.0;
    std::vector<Vec3>   gradP_;
    std::vector<double> regionConductivity_;
    std::vector<char>   wallCell_;
    std::vector<Vec3>   Unew_;
    std::vector<double> pNew_, Tnew_, kNew_, omegaNew_;
    std::vector<double> interfaceTemperature_;   //!< partitioned CHT interface values
    std::vector<double> interfaceConductance_;   //!< partitioned CHT conductances H*A

    // helpers ---------------------------------------------------------------------
    void computeGradients();
    double cellViscosity(int cell) const;
    void assembleMomentum(double dt);
    void assemblePressure();
    void assembleEnergy(double dt);
    void assembleTurbulenceScalar(bool omegaEquation, double dt);
    void updateMassFluxes();
    void updateWallTreatment();
    void updatePressureGradient();
    void computeCHTInterfaces(std::vector<double>& interfaceT, std::vector<double>& conductance) const;
    void relaxCHTInterfaces();
    int    adjacentCell(int cell, int face) const;
    double wallNormalDistance(int face) const;
    double wallTangentialSpeed(int face, const Vec3& u) const;
};

} // namespace mt
