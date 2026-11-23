// =====================================================================================
//  mt::verify -- engineering verification harness for Cluster A.
//
//  Benchmarks
//    1. Composite fluid-solid CHT : analytic series-resistance conduction and
//       convection, monolithic vs partitioned, conformal and non-conformal.
//    2. All-y+ wall treatment      : friction-velocity round trip, omega blending,
//       Kader wall heat flux.
//    3. Turbulent channel          : SST k-omega + All-y+ wall functions against the
//       log law / Reichardt profile; positivity of k and omega.
//    4. LES WALE channel           : time averaged profile in the log layer.
//    5. Anisotropic porous media   : Darcy-Forchheimer pressure drop.
//    6. Radiation                  : fvDOM against the exact 1-D transport solution,
//       P1 against the analytic diffusion-limit solution.
//
//  Usage :  mt_verify [output.json]
// =====================================================================================
#include "mt/Mesh.hpp"
#include "mt/Physics.hpp"
#include "mt/Solver.hpp"
#include "mt/Sparse.hpp"
#include "mt/Turbulence.hpp"
#include "mt/WallTreatment.hpp"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace mt;

namespace {

// -------------------------------------------------------------------------------------
//  helpers
// -------------------------------------------------------------------------------------
struct Check {
    std::string name;
    double      value = 0.0;
    double      target = 0.0;
    double      error = 0.0;
    double      relError = 0.0;
    std::string unit;
    bool        pass = false;
    std::string note;
};

std::vector<Check> g_checks;

Check makeCheck(const std::string& name, double value, double target, const std::string& unit,
                double tolerance, const std::string& note = "") {
    Check c;
    c.name = name;
    c.value = value;
    c.target = target;
    c.error = value - target;
    c.relError = (std::abs(target) > 1e-30) ? std::abs(c.error / target) : std::abs(c.error);
    c.unit = unit;
    c.pass = c.relError <= tolerance;
    c.note = note;
    g_checks.push_back(c);
    return c;
}

Json checkJson(const Check& c) {
    Json j = Json::object();
    j.set("name", c.name);
    j.set("value", c.value);
    j.set("target", c.target);
    j.set("error", c.error);
    j.set("relError", c.relError);
    j.set("unit", c.unit);
    j.set("pass", c.pass);
    if (!c.note.empty()) j.set("note", c.note);
    return j;
}

std::vector<double> linspace(double a, double b, int n) {
    std::vector<double> v(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<std::size_t>(i)] = a + (b - a) * i / std::max(1, n - 1);
    return v;
}

//! tanh-clustered channel coordinates (wall refinement)
std::vector<double> channelCoordinates(int n, double height, double beta) {
    std::vector<double> y(static_cast<std::size_t>(n) + 1);
    const double tb = std::tanh(beta);
    for (int i = 0; i <= n; ++i)
        y[static_cast<std::size_t>(i)] = 0.5 * height * std::tanh(beta * i / static_cast<double>(n)) / tb;
    y[0] = 0.0;
    y[static_cast<std::size_t>(n)] = height;
    return y;
}

//! symmetric boundary condition helper : flip the type of a boundary condition
void setBcType(Mesh& mesh, int bcId, BCType type) {
    if (bcId >= 0 && bcId < static_cast<int>(mesh.bcs.size())) mesh.bcs[static_cast<std::size_t>(bcId)].type = type;
}

// =====================================================================================
//  1. Composite fluid-solid CHT
// =====================================================================================
struct CompositeResult {
    double heatFlux = 0.0;
    double interfaceTemperature = 0.0;
    double maxNodalError = 0.0;
    int    iterations = 0;
};

CompositeResult runCompositeCHT(int nFluid, int nSolid, double Lf, double Ls,
                                double kf, double ks, double Thot, double Tcold,
                                double hConv, double Tinf, bool monolithic, bool convective) {
    Mesh mesh;
    const double lx = 1.0, lz = 1.0;
    const double dyf = Lf / nFluid, dys = Ls / nSolid;

    // ---- boundary conditions ------------------------------------------------------
    BoundaryCondition bcFluidIn;
    bcFluidIn.type = BCType::Wall;
    bcFluidIn.name = "fluid-inlet";
    if (convective) { bcFluidIn.specifiedConvective = true; bcFluidIn.heatTransferCoefficient = hConv; bcFluidIn.ambientTemperature = Tinf; }
    else { bcFluidIn.specifiedTemperature = true; bcFluidIn.value = Thot; }
    const int idFluidIn = mesh.addBoundaryCondition(bcFluidIn);

    BoundaryCondition bcSolidOut;
    bcSolidOut.type = BCType::Wall;
    bcSolidOut.name = "solid-outlet";
    bcSolidOut.specifiedTemperature = true;
    bcSolidOut.value = Tcold;
    const int idSolidOut = mesh.addBoundaryCondition(bcSolidOut);

    BoundaryCondition bcIfaceF;
    bcIfaceF.type = BCType::InterfaceCHT;
    bcIfaceF.name = "cht-fluid";
    const int idIfaceF = mesh.addBoundaryCondition(bcIfaceF);

    BoundaryCondition bcIfaceS;
    bcIfaceS.type = BCType::InterfaceCHT;
    bcIfaceS.name = "cht-solid";
    const int idIfaceS = mesh.addBoundaryCondition(bcIfaceS);

    BoundaryCondition bcSym;
    bcSym.type = BCType::Symmetry;
    bcSym.name = "symmetry";
    const int idSym = mesh.addBoundaryCondition(bcSym);

    // ---- cells --------------------------------------------------------------------
    for (int i = 0; i < nFluid; ++i)
        mesh.addCell(Vec3{0.5 * lx, (i + 0.5) * dyf, 0.5 * lz}, lx * lz * dyf, 0, 0);
    for (int i = 0; i < nSolid; ++i)
        mesh.addCell(Vec3{0.5 * lx, Lf + (i + 0.5) * dys, 0.5 * lz}, lx * lz * dys, 1, 0);

    // ---- interior faces -------------------------------------------------------------
    for (int i = 0; i + 1 < nFluid; ++i)
        mesh.addFace(i, i + 1, Vec3{0.5 * lx, (i + 1) * dyf, 0.5 * lz}, Vec3{0.0, lx * lz, 0.0}, -1);
    for (int i = 0; i + 1 < nSolid; ++i)
        mesh.addFace(nFluid + i, nFluid + i + 1, Vec3{0.5 * lx, Lf + (i + 1) * dys, 0.5 * lz}, Vec3{0.0, lx * lz, 0.0}, -1);

    // ---- the conformal twin interface : S_s = -S_f ------------------------------------
    const int faceIF = mesh.addFace(nFluid - 1, -1, Vec3{0.5 * lx, Lf, 0.5 * lz}, Vec3{0.0, lx * lz, 0.0}, idIfaceF);
    const int faceIS = mesh.addFace(nFluid, -1, Vec3{0.5 * lx, Lf, 0.5 * lz}, Vec3{0.0, -lx * lz, 0.0}, idIfaceS);

    // ---- outer boundaries -------------------------------------------------------------
    mesh.addFace(0, -1, Vec3{0.5 * lx, 0.0, 0.5 * lz}, Vec3{0.0, -lx * lz, 0.0}, idFluidIn);
    mesh.addFace(nFluid + nSolid - 1, -1, Vec3{0.5 * lx, Lf + Ls, 0.5 * lz}, Vec3{0.0, lx * lz, 0.0}, idSolidOut);
    for (int c = 0; c < nFluid + nSolid; ++c) {
        const double yc = mesh.cellCentre[static_cast<std::size_t>(c)].y;
        const double dy = (c < nFluid) ? dyf : dys;
        mesh.addFace(c, -1, Vec3{0.0, yc, 0.5 * lz}, Vec3{-lz * dy, 0.0, 0.0}, idSym);
        mesh.addFace(c, -1, Vec3{lx, yc, 0.5 * lz}, Vec3{lz * dy, 0.0, 0.0}, idSym);
        mesh.addFace(c, -1, Vec3{0.5 * lx, yc, 0.0}, Vec3{0.0, 0.0, -lx * dy}, idSym);
        mesh.addFace(c, -1, Vec3{0.5 * lx, yc, lz}, Vec3{0.0, 0.0, lx * dy}, idSym);
    }
    mesh.computeGeometry();

    // ---- solver ----------------------------------------------------------------------
    SolverConfig cfg;
    cfg.turbulence = TurbModel::None;
    cfg.solveEnergy = true;
    cfg.solvePressure = false;
    cfg.rho = 1.0; cfg.mu = 1.0e-3; cfg.cp = 1000.0;
    cfg.pseudoTimeStep = 0.05;
    cfg.maxIterations = 4000;
    cfg.tolerance = 1.0e-10;
    cfg.underRelaxT = 1.0;
    cfg.chtMonolithic = monolithic;
    CHTRegionProperties fluid;
    fluid.id = 0; fluid.name = "fluid"; fluid.conductivity = kf; fluid.density = 1000.0; fluid.specificHeat = 1000.0;
    CHTRegionProperties solid;
    solid.id = 1; solid.name = "solid"; solid.conductivity = ks; solid.density = 7800.0; solid.specificHeat = 500.0;
    cfg.regions = {fluid, solid};
    cfg.cht.addConformal(faceIF, faceIS);

    Solver solver(mesh, cfg);
    solver.initialise();
    const SolverDiagnostics diag = solver.runSteady();

    // ---- evaluation -------------------------------------------------------------------
    CHTCoupling probe;
    probe.addConformal(faceIF, faceIS);
    const std::vector<Vec3> gradT = mesh.cellGradient(solver.T);
    const CHTInterfaceState st = probe.evaluate(mesh, faceIF, {faceIS}, {mesh.faces.area[static_cast<std::size_t>(faceIF)]},
                                                 solver.T, gradT, fluid, solid);

    // analytic profile
    const double Rf = Lf / kf, Rs = Ls / ks;
    const double qAnalytic = convective ? (Tinf - Tcold) / (1.0 / hConv + Rf + Rs)
                                        : (Thot - Tcold) / (Rf + Rs);
    const double TinterfaceAnalytic = convective ? Tinf - qAnalytic * (1.0 / hConv + Rf)
                                                  : Thot - qAnalytic * Rf;

    // nodal error against the analytic linear profile
    double maxErr = 0.0;
    for (int c = 0; c < mesh.nCells; ++c) {
        const double y = mesh.cellCentre[static_cast<std::size_t>(c)].y;
        double Texact;
        if (c < nFluid) {
            const double dT = (convective ? Tinf : Thot) - qAnalytic * ((convective ? 1.0 / hConv : 0.0) + y / kf);
            Texact = dT;
        } else {
            Texact = TinterfaceAnalytic - qAnalytic * (y - Lf) / ks;
        }
        maxErr = std::max(maxErr, std::abs(solver.T[static_cast<std::size_t>(c)] - Texact));
    }

    if (std::getenv("MT_DEBUG_CHT")) {
        std::fprintf(stderr, "CHT q=%g Tint=%g H=%g skew=%g\n", st.heatFlux, st.interfaceTemperature, st.conductance, st.skewCorrection);
        for (int c = 0; c < mesh.nCells; ++c)
            std::fprintf(stderr, "  cell %d y=%g T=%g\n", c, mesh.cellCentre[c].y, solver.T[c]);
    }
    CompositeResult res;
    res.heatFlux = -st.heatFlux;             // flux from the fluid into the solid
    res.interfaceTemperature = st.interfaceTemperature;
    res.maxNodalError = maxErr;
    res.iterations = diag.iterations;
    return res;
}

Json benchmarkCHT() {
    Json j = Json::object();

    // ---- (a) pure conduction : analytic series resistance --------------------------
    {
        const double kf = 60.0, ks = 120.0, Lf = 0.4, Ls = 0.3, Thot = 800.0, Tcold = 300.0;
        const CompositeResult r = runCompositeCHT(40, 30, Lf, Ls, kf, ks, Thot, Tcold, 0.0, 0.0, true, false);
        const double qAnalytic = (Thot - Tcold) / (Lf / kf + Ls / ks);
        const double TiAnalytic = Thot - qAnalytic * Lf / kf;
        j.set("conduction", Json::object()
            .set("heatFluxFvm", r.heatFlux)
            .set("heatFluxAnalytic", qAnalytic)
            .set("interfaceTemperatureFvm", r.interfaceTemperature)
            .set("interfaceTemperatureAnalytic", TiAnalytic)
            .set("maxNodalError", r.maxNodalError)
            .set("iterations", r.iterations));
        makeCheck("CHT conduction heat flux", r.heatFlux, qAnalytic, "W/m2", 1.0e-6, "monolithic harmonic interface");
        makeCheck("CHT conduction interface temperature", r.interfaceTemperature, TiAnalytic, "K", 1.0e-6);
    }

    // ---- (b) convection + conduction : series resistance with a Robin BC -----------
    {
        const double kf = 60.0, ks = 120.0, Lf = 0.4, Ls = 0.3, Tcold = 300.0, h = 250.0, Tinf = 1000.0;
        const CompositeResult r = runCompositeCHT(40, 30, Lf, Ls, kf, ks, 0.0, Tcold, h, Tinf, true, true);
        const double qAnalytic = (Tinf - Tcold) / (1.0 / h + Lf / kf + Ls / ks);
        const double TiAnalytic = Tinf - qAnalytic * (1.0 / h + Lf / kf);
        j.set("convective", Json::object()
            .set("heatFluxFvm", r.heatFlux)
            .set("heatFluxAnalytic", qAnalytic)
            .set("interfaceTemperatureFvm", r.interfaceTemperature)
            .set("interfaceTemperatureAnalytic", TiAnalytic)
            .set("maxNodalError", r.maxNodalError)
            .set("iterations", r.iterations));
        makeCheck("CHT convective+conductive heat flux", r.heatFlux, qAnalytic, "W/m2", 5.0e-4, "Robin + solid conduction");
    }

    // ---- (c) partitioned (Robin-Robin) vs monolithic -------------------------------
    {
        const double kf = 60.0, ks = 120.0, Lf = 0.4, Ls = 0.3, Tcold = 300.0, h = 250.0, Tinf = 1000.0;
        const CompositeResult mono = runCompositeCHT(40, 30, Lf, Ls, kf, ks, 0.0, Tcold, h, Tinf, true, true);
        const CompositeResult part = runCompositeCHT(40, 30, Lf, Ls, kf, ks, 0.0, Tcold, h, Tinf, false, true);
        j.set("partitioned", Json::object()
            .set("monolithicFlux", mono.heatFlux)
            .set("partitionedFlux", part.heatFlux)
            .set("monolithicIterations", mono.iterations)
            .set("partitionedIterations", part.iterations));
        makeCheck("CHT partitioned vs monolithic flux", part.heatFlux, mono.heatFlux, "W/m2", 5.0e-2, "under-relaxed Robin-Robin");
    }

    // ---- (d) non-conformal (Supermesh-AMI) interface --------------------------------
    {
        // A fluid face overlapping two solid faces : the interface conductance is the
        // area weighted harmonic average of the two overlap contributions.
        Mesh mesh;
        BoundaryCondition bcw; bcw.type = BCType::Wall;
        const int idw = mesh.addBoundaryCondition(bcw);
        mesh.addCell(Vec3{0.5, 0.05, 0.5}, 1.0, 0, 0);
        mesh.addCell(Vec3{0.5, 0.15, 0.5}, 1.0, 1, 0);
        const int fF = mesh.addFace(0, -1, Vec3{0.5, 0.1, 0.5}, Vec3{0, 1, 0}, idw);
        const int fS1 = mesh.addFace(1, -1, Vec3{0.25, 0.1, 0.5}, Vec3{0, -1, 0}, idw);
        const int fS2 = mesh.addFace(1, -1, Vec3{0.75, 0.1, 0.5}, Vec3{0, -1, 0}, idw);
        mesh.computeGeometry();

        CHTRegionProperties fluid; fluid.conductivity = 60.0;
        CHTRegionProperties solid; solid.conductivity = 120.0;
        std::vector<double> T = {400.0, 350.0};
        std::vector<Vec3> gradT = {Vec3{0, 0, 0}, Vec3{0, 0, 0}};
        CHTCoupling coupling;
        const std::vector<int> solidFaces = {fS1, fS2};
        const std::vector<double> areas = {0.5, 0.5};
        const CHTInterfaceState st = coupling.evaluate(mesh, fF, solidFaces, areas, T, gradT, fluid, solid);

        // analytic : two independent half-face couplings in parallel
        const double df = 0.05, ds = 0.05;
        const double Gf = fluid.conductivity / df;
        const double Gs = solid.conductivity / ds;
        const double H = Gf * Gs / (Gf + Gs);
        const double qAnalytic = H * (350.0 - 400.0);
        j.set("nonconformal", Json::object()
            .set("heatFluxFvm", st.heatFlux)
            .set("heatFluxAnalytic", qAnalytic)
            .set("interfaceTemperature", st.interfaceTemperature)
            .set("conductance", st.conductance));
        makeCheck("CHT non-conformal (AMI) interface flux", st.heatFlux, qAnalytic, "W/m2", 1.0e-8, "Supermesh overlap areas");
    }
    return j;
}

// =====================================================================================
//  2. All-y+ wall treatment unit verification
// =====================================================================================
Json benchmarkWallTreatment() {
    Json j = Json::object();
    AllYPlusWallTreatment wall;
    const double nu = 5.0e-4;

    Json fv = Json::array();
    Json om = Json::array();
    Json hf = Json::array();
    const double yPlusList[] = {0.5, 1.0, 5.0, 11.0, 30.0, 100.0, 500.0, 2000.0, 5000.0};
    double maxRel = 0.0;
    for (double yPlus : yPlusList) {
        const double uPlus = wall.blendedProfile(yPlus);
        // reconstruct u_tau from (y_P, u_parallel)
        const double uTau = 1.0;
        const double yP = yPlus * nu / uTau;
        const double uPar = uPlus * uTau;
        const double uTauBack = wall.frictionVelocity(yP, uPar, nu);
        const double rel = std::abs(uTauBack - uTau) / uTau;
        maxRel = std::max(maxRel, rel);

        const double omegaVis = 6.0 * nu / (wall.config().beta1 * yP * yP);
        const double omegaLog = uTau / (std::sqrt(wall.config().betaStar) * wall.config().kappa * yP);
        const double omegaBlend = std::sqrt(omegaVis * omegaVis + omegaLog * omegaLog);

        const double rho = 1.225, cp = 1005.0;
        const double q = wall.wallHeatFlux(yP, 400.0, 350.0, uTau, nu, rho, cp);
        const double hEff = q / (400.0 - 350.0);

        fv.push(Json::object().set("yPlus", yPlus).set("uPlus", uPlus).set("uTauRecovered", uTauBack).set("relError", rel));
        om.push(Json::object().set("yPlus", yPlus).set("omegaVis", omegaVis).set("omegaLog", omegaLog).set("omegaBlend", omegaBlend));
        hf.push(Json::object().set("yPlus", yPlus).set("heatFlux", q).set("hEffective", hEff));
    }
    j.set("frictionVelocity", fv);
    j.set("omegaBlend", om);
    j.set("wallHeatFlux", hf);
    makeCheck("All-y+ friction velocity round trip", maxRel, 0.0, "-", 5.0e-3, "0.5 <= y+ <= 5000");
    return j;
}

// =====================================================================================
//  3. Turbulent channel : SST k-omega with All-y+ wall treatment
// =====================================================================================
struct ChannelResult {
    std::vector<double> y, yPlus, uPlus, logLaw, reichardt, u;
    double uTauComputed = 0.0, uTauTarget = 0.0;
    double tauWall = 0.0, tauWallTarget = 0.0;
    double cf = 0.0, uBulk = 0.0;
    double logLawRms = 0.0, reichardtRms = 0.0;
    double minK = 0.0, minOmega = 0.0;
    int    limiterCells = 0;
    int    iterations = 0;
    bool   converged = false;
    double maxCellSize = 0.0;
};

ChannelResult runChannel(int nCells, double halfHeight, double nu, double reTau, bool wale) {
    ChannelResult res;
    const double height = 2.0 * halfHeight;
    const double rho = 1.225;
    const double mu = rho * nu;
    const double uTauTarget = reTau * nu / halfHeight;
    const double G = uTauTarget * uTauTarget / halfHeight;

    Mesh mesh;
    const std::vector<double> yc = channelCoordinates(nCells, height, wale ? 5.0 : 3.5);
    MeshBuilder::buildChannel(mesh, nCells, halfHeight, 1.0, 1.0, yc);

    SolverConfig cfg;
    cfg.rho = rho;
    cfg.mu = mu;
    cfg.turbulence = wale ? TurbModel::WALE : TurbModel::SST;
    cfg.turb.nu = nu;
    cfg.turb.rho = rho;
    cfg.turb.kInf = 1.0e-4;
    cfg.turb.omegaInf = 10.0;
    cfg.wall.kappa = 0.41;
    cfg.wall.Pr = 0.71;
    cfg.prescribedPressureGradient = true;
    cfg.pressureGradient = Vec3{-G, 0.0, 0.0};
    cfg.solvePressure = false;
    cfg.solveEnergy = false;
    cfg.wallFunctions = !wale;
    cfg.pseudoTimeStep = 0.01;
    cfg.maxIterations = 4000;
    cfg.tolerance = 1.0e-7;
    cfg.underRelaxU = 0.7;
    cfg.underRelaxK = 0.4;
    cfg.underRelaxW = 0.4;

    Solver solver(mesh, cfg);
    solver.initialise();

    if (!wale) {
        solver.runSteady();
    } else {
        // ---- transient LES with time averaging ------------------------------------
        cfg.transient = true;
        cfg.timeStep = 5.0e-4;
        cfg.pisoIterations = 2;
        Solver les(mesh, cfg);
        les.initialise();
        std::vector<double> accum(static_cast<std::size_t>(mesh.nCells), 0.0);
        int samples = 0;
        for (int step = 0; step < 2500; ++step) {
            les.advance();
            if (step > 1500) {
                for (int c = 0; c < mesh.nCells; ++c) accum[static_cast<std::size_t>(c)] += les.U[static_cast<std::size_t>(c)].x;
                ++samples;
            }
        }
        for (int c = 0; c < mesh.nCells; ++c)
            solver.U[static_cast<std::size_t>(c)].x = (samples > 0) ? accum[static_cast<std::size_t>(c)] / samples : 0.0;
    }

    // ---- wall shear ---------------------------------------------------------------
    double tauSum = 0.0;
    int tauCount = 0;
    for (int f = 0; f < mesh.nFaces(); ++f) {
        const int bc = mesh.faces.bcId[static_cast<std::size_t>(f)];
        if (bc < 0) continue;
        if (mesh.bcs[static_cast<std::size_t>(bc)].type != BCType::Wall) continue;
        tauSum += solver.wallShear[static_cast<std::size_t>(f)];
        ++tauCount;
    }
    res.tauWall = (tauCount > 0) ? tauSum / tauCount : 0.0;
    res.tauWallTarget = G * halfHeight;
    res.uTauComputed = std::sqrt(std::max(res.tauWall, 0.0) / rho);
    res.uTauTarget = uTauTarget;

    // ---- profile ------------------------------------------------------------------
    double uBulk = 0.0;
    for (int c = 0; c < mesh.nCells; ++c) uBulk += solver.U[static_cast<std::size_t>(c)].x;
    uBulk /= mesh.nCells;
    res.uBulk = uBulk;
    res.cf = 2.0 * res.tauWall / (rho * uBulk * uBulk);

    double sumLog = 0.0, sumRei = 0.0;
    int countLog = 0;
    AllYPlusWallTreatment wall;
    for (int c = 0; c < mesh.nCells; ++c) {
        const double y = mesh.cellCentre[static_cast<std::size_t>(c)].y;
        const double dist = std::min(y, height - y);
        const double yPlus = dist * res.uTauComputed / nu;
        const double u = solver.U[static_cast<std::size_t>(c)].x;
        const double uPlus = u / res.uTauComputed;
        const double logLaw = (1.0 / 0.41) * std::log(std::max(yPlus, 1.0)) + 5.5;
        const double rei = wall.reichardtProfile(yPlus);
        res.y.push_back(dist);
        res.yPlus.push_back(yPlus);
        res.uPlus.push_back(uPlus);
        res.logLaw.push_back(logLaw);
        res.reichardt.push_back(rei);
        if (yPlus > 30.0 && yPlus < 3000.0) {
            const double e1 = uPlus - logLaw, e2 = uPlus - rei;
            sumLog += e1 * e1; sumRei += e2 * e2; ++countLog;
        }
        res.maxCellSize = std::max(res.maxCellSize, mesh.cellSize(c));
    }
    if (countLog > 0) {
        res.logLawRms = std::sqrt(sumLog / countLog);
        res.reichardtRms = std::sqrt(sumRei / countLog);
    }
    const auto d = solver.turbulenceModel().diagnostics(solver.k, solver.omega);
    res.minK = d.minK;
    res.minOmega = d.minOmega;
    res.limiterCells = d.limiterCells;
    return res;
}

Json benchmarkChannel() {
    Json j = Json::object();

    // ---- SST k-omega + All-y+ wall functions --------------------------------------
    const ChannelResult r = runChannel(60, 0.5, 5.0e-4, 1000.0, false);
    Json profile = Json::array();
    for (std::size_t i = 0; i < r.y.size(); ++i)
        profile.push(Json::object()
            .set("y", r.y[i]).set("yPlus", r.yPlus[i]).set("uPlus", r.uPlus[i])
            .set("logLaw", r.logLaw[i]).set("reichardt", r.reichardt[i]));
    j.set("sst", Json::object()
        .set("profile", profile)
        .set("uTauComputed", r.uTauComputed)
        .set("uTauTarget", r.uTauTarget)
        .set("tauWall", r.tauWall)
        .set("tauWallTarget", r.tauWallTarget)
        .set("cf", r.cf)
        .set("uBulk", r.uBulk)
        .set("logLawRms", r.logLawRms)
        .set("reichardtRms", r.reichardtRms)
        .set("minK", r.minK)
        .set("minOmega", r.minOmega)
        .set("limiterCells", r.limiterCells));

    makeCheck("Channel wall shear (SST + All-y+)", r.tauWall, r.tauWallTarget, "Pa", 5.0e-2, "tau_w = G h");
    makeCheck("Channel log-law RMS error", r.logLawRms, 0.0, "u+", 0.65, "30 < y+ < 3000");
    Check ck = makeCheck("Turbulence positivity k", r.minK, 0.0, "m2/s2", 0.0, "min k > 0 over the whole run");
    ck.pass = (r.minK > 0.0);
    Check cw = makeCheck("Turbulence positivity omega", r.minOmega, 0.0, "1/s", 0.0, "min omega > 0 over the whole run");
    cw.pass = (r.minOmega > 0.0);

    // ---- WALE LES ------------------------------------------------------------------
    const ChannelResult les = runChannel(80, 0.5, 5.0e-4, 1000.0, true);
    Json lesProfile = Json::array();
    for (std::size_t i = 0; i < les.y.size(); ++i)
        lesProfile.push(Json::object()
            .set("y", les.y[i]).set("yPlus", les.yPlus[i]).set("uPlus", les.uPlus[i])
            .set("logLaw", les.logLaw[i]).set("reichardt", les.reichardt[i]));
    j.set("wale", Json::object()
        .set("profile", lesProfile)
        .set("uTauComputed", les.uTauComputed)
        .set("uTauTarget", les.uTauTarget)
        .set("tauWall", les.tauWall)
        .set("tauWallTarget", les.tauWallTarget)
        .set("logLawRms", les.logLawRms)
        .set("reichardtRms", les.reichardtRms));

    // ---- WALE formula unit test ---------------------------------------------------
    {
        TurbulenceConfig tc;
        tc.model = TurbModel::WALE;
        tc.waleCw = 0.5;
        Mesh dummy;
        TurbulenceModel model(dummy, tc);
        SymMat3 Sd;  // unit shear : S_xx = 1
        Sd.xx = 1.0;
        SymMat3 Sbar; // filtered strain = 0.2 * Sd
        Sbar.xx = 0.2;
        const double delta = 0.1;
        const double nut = model.waleSgs(delta, Sd, Sbar);
        const double sd2 = 1.0, sb2 = 0.04;
        const double expect = (0.5 * delta) * (0.5 * delta) * std::pow(sd2, 1.5)
                            / (std::pow(sb2, 2.5) + std::pow(sd2, 1.25));
        j.set("waleFormula", Json::object().set("nuSgs", nut).set("nuSgsAnalytic", expect));
        makeCheck("WALE sub-grid viscosity", nut, expect, "m2/s", 1.0e-10, "closed form check");
    }
    return j;
}

// =====================================================================================
//  4. Anisotropic porous media : Darcy-Forchheimer
// =====================================================================================
Json benchmarkPorous() {
    Json j = Json::object();

    auto runCase = [&](double dAlong, const std::string& label) {
        const double L = 1.0, lx = 1.0, lz = 1.0;
        const int N = 40;
        Mesh mesh;
        BoundaryCondition bcIn; bcIn.type = BCType::Inlet; bcIn.name = "porous-inlet";
        bcIn.velocity = Vec3{0.0, 0.5, 0.0};
        const int idIn = mesh.addBoundaryCondition(bcIn);
        BoundaryCondition bcOut; bcOut.type = BCType::Outlet; bcOut.name = "porous-outlet";
        bcOut.pressure = 0.0;
        const int idOut = mesh.addBoundaryCondition(bcOut);
        BoundaryCondition bcSym; bcSym.type = BCType::Symmetry; bcSym.name = "sym";
        const int idSym = mesh.addBoundaryCondition(bcSym);

        const double dy = L / N;
        for (int c = 0; c < N; ++c) mesh.addCell(Vec3{0.5 * lx, (c + 0.5) * dy, 0.5 * lz}, lx * lz * dy, 0, 1);
        for (int c = 0; c + 1 < N; ++c)
            mesh.addFace(c, c + 1, Vec3{0.5 * lx, (c + 1) * dy, 0.5 * lz}, Vec3{0, lx * lz, 0}, -1);
        mesh.addFace(0, -1, Vec3{0.5 * lx, 0.0, 0.5 * lz}, Vec3{0, -lx * lz, 0}, idIn);
        mesh.addFace(N - 1, -1, Vec3{0.5 * lx, L, 0.5 * lz}, Vec3{0, lx * lz, 0}, idOut);
        for (int c = 0; c < N; ++c) {
            const double yc = mesh.cellCentre[static_cast<std::size_t>(c)].y;
            mesh.addFace(c, -1, Vec3{0.0, yc, 0.5 * lz}, Vec3{-lz * dy, 0, 0}, idSym);
            mesh.addFace(c, -1, Vec3{lx, yc, 0.5 * lz}, Vec3{lz * dy, 0, 0}, idSym);
            mesh.addFace(c, -1, Vec3{0.5 * lx, yc, 0.0}, Vec3{0, 0, -lx * dy}, idSym);
            mesh.addFace(c, -1, Vec3{0.5 * lx, yc, lz}, Vec3{0, 0, lx * dy}, idSym);
        }
        mesh.computeGeometry();

        SolverConfig cfg;
        cfg.rho = 1.225; cfg.mu = 1.81e-5;
        cfg.turbulence = TurbModel::None;
        cfg.solveEnergy = false;
        cfg.pseudoTimeStep = 0.05;
        cfg.maxIterations = 3000;
        cfg.tolerance = 1.0e-8;
        cfg.underRelaxU = 0.5;
        cfg.underRelaxP = 0.3;

        PorousZone zone;
        zone.id = 0; zone.name = "catalyst-bed";
        zone.cells.resize(static_cast<std::size_t>(N));
        for (int c = 0; c < N; ++c) zone.cells[static_cast<std::size_t>(c)] = c;
        zone.permeability = 1.0e-9;
        zone.forchheimer = 1.0e4;
        zone.porosity = 0.4;
        zone.d1 = 1.0; zone.d2 = dAlong; zone.d3 = 1.0;   // the flow is along y
        zone.axis1 = Vec3{1, 0, 0}; zone.axis2 = Vec3{0, 1, 0}; zone.axis3 = Vec3{0, 0, 1};
        cfg.porous.addZone(zone);

        Solver solver(mesh, cfg);
        solver.initialise();
        solver.runSteady();

        const double uIn = 0.5;
        const double dpAnalytic = cfg.mu * L * uIn / (zone.permeability * dAlong)
                                + 0.5 * cfg.rho * zone.forchheimer * L * uIn * uIn;
        const double dpFvm = solver.p[0] - solver.p[static_cast<std::size_t>(N - 1)];

        Json c = Json::object()
            .set("pressureDropFvm", dpFvm)
            .set("pressureDropAnalytic", dpAnalytic)
            .set("anisotropyAlongFlow", dAlong)
            .set("permeability", zone.permeability)
            .set("forchheimer", zone.forchheimer)
            .set("inletVelocity", uIn)
            .set("meanVelocity", [&] {
                double s = 0.0; for (int c2 = 0; c2 < N; ++c2) s += solver.U[static_cast<std::size_t>(c2)].y;
                return s / N; }());
        j.set(label, c);
        makeCheck("Porous Darcy-Forchheimer dP (" + label + ")", dpFvm, dpAnalytic, "Pa", 2.0e-2, "anisotropic porous zone");
    };

    runCase(1.0, "isotropic");
    runCase(0.25, "anisotropic");
    return j;
}

// =====================================================================================
//  5. Radiation : P1 and fvDOM in a 1-D slab
// =====================================================================================
Json benchmarkRadiation() {
    Json j = Json::object();
    const double L = 1.0, lx = 1.0, lz = 1.0;
    const int N = 200;
    const double Tgas = 1000.0, Tw1 = 1500.0, Tw2 = 800.0;
    const double kappaA = 2.0;

    Mesh mesh;
    BoundaryCondition bcw; bcw.type = BCType::Wall; bcw.name = "rad-wall";
    const int idw = mesh.addBoundaryCondition(bcw);
    BoundaryCondition bcSym; bcSym.type = BCType::Symmetry; bcSym.name = "sym";
    const int idSym = mesh.addBoundaryCondition(bcSym);
    const double dy = L / N;
    for (int c = 0; c < N; ++c) mesh.addCell(Vec3{0.5 * lx, (c + 0.5) * dy, 0.5 * lz}, lx * lz * dy, 0, 0);
    for (int c = 0; c + 1 < N; ++c)
        mesh.addFace(c, c + 1, Vec3{0.5 * lx, (c + 1) * dy, 0.5 * lz}, Vec3{0, lx * lz, 0}, -1);
    mesh.addFace(0, -1, Vec3{0.5 * lx, 0.0, 0.5 * lz}, Vec3{0, -lx * lz, 0}, idw);
    mesh.addFace(N - 1, -1, Vec3{0.5 * lx, L, 0.5 * lz}, Vec3{0, lx * lz, 0}, idw);
    for (int c = 0; c < N; ++c) {
        const double yc = mesh.cellCentre[static_cast<std::size_t>(c)].y;
        mesh.addFace(c, -1, Vec3{0.0, yc, 0.5 * lz}, Vec3{-lz * dy, 0, 0}, idSym);
        mesh.addFace(c, -1, Vec3{lx, yc, 0.5 * lz}, Vec3{lz * dy, 0, 0}, idSym);
        mesh.addFace(c, -1, Vec3{0.5 * lx, yc, 0.0}, Vec3{0, 0, -lx * dy}, idSym);
        mesh.addFace(c, -1, Vec3{0.5 * lx, yc, lz}, Vec3{0, 0, lx * dy}, idSym);
    }
    mesh.computeGeometry();

    std::vector<double> T(static_cast<std::size_t>(N), Tgas);
    std::vector<double> wallT(static_cast<std::size_t>(mesh.nFaces()), Tgas);
    for (int f = 0; f < mesh.nFaces(); ++f) {
        const int bc = mesh.faces.bcId[static_cast<std::size_t>(f)];
        if (bc < 0) continue;
        const Vec3& n = mesh.faces.unitNormal[static_cast<std::size_t>(f)];
        wallT[static_cast<std::size_t>(f)] = (n.y < 0.0) ? Tw1 : Tw2;
    }

    RadiationSettings settings;
    settings.absorptionCoefficient = kappaA;
    settings.scatteringCoefficient = 0.0;
    settings.wallEmissivity = 1.0;
    settings.quadratureOrder = 2;
    settings.axis = 1;

    RadiationModule rad;
    const DOMResult dom = rad.solveDOM(mesh, T, settings, &wallT);
    const std::vector<double> p1 = rad.solveP1(mesh, T, settings, &wallT);

    // ---- exact 1-D transport solution (two-stream, black walls) ---------------------
    const double Ib = kPi * kStefanBoltzmann * std::pow(Tgas, 4.0);
    const double I1 = kPi * kStefanBoltzmann * std::pow(Tw1, 4.0);
    const double I2 = kPi * kStefanBoltzmann * std::pow(Tw2, 4.0);
    Json domProfile = Json::array();
    double maxErrPlus = 0.0, maxErrMinus = 0.0;
    for (int c = 0; c < N; c += 10) {
        const double y = mesh.cellCentre[static_cast<std::size_t>(c)].y;
        const double IplusExact = Ib + (I1 - Ib) * std::exp(-kappaA * y);
        const double IminusExact = Ib + (I2 - Ib) * std::exp(-kappaA * (L - y));
        const double ep = std::abs(dom.intensity[0][static_cast<std::size_t>(c)] - IplusExact) / IplusExact;
        const double em = std::abs(dom.intensity[1][static_cast<std::size_t>(c)] - IminusExact) / IminusExact;
        maxErrPlus = std::max(maxErrPlus, ep);
        maxErrMinus = std::max(maxErrMinus, em);
        domProfile.push(Json::object()
            .set("y", y)
            .set("Iplus", dom.intensity[0][static_cast<std::size_t>(c)])
            .set("IplusExact", IplusExact)
            .set("Iminus", dom.intensity[1][static_cast<std::size_t>(c)])
            .set("IminusExact", IminusExact)
            .set("G", dom.G[static_cast<std::size_t>(c)]));
    }

    // ---- analytic P1 (diffusion limit) solution -------------------------------------
    const double gamma = 1.0 / (3.0 * kappaA);
    const double a = kappaA;
    const double Gp = 4.0 * kPi * kStefanBoltzmann * std::pow(Tgas, 4.0);
    const double Gleft = 0.5 * kPi * kStefanBoltzmann * std::pow(Tw1, 4.0);
    const double Gright = 0.5 * kPi * kStefanBoltzmann * std::pow(Tw2, 4.0);
    const double kk = std::sqrt(a / gamma);
    // G(y) = Gp + A cosh(kk y) + B sinh(kk y)
    const double A = Gleft - Gp;
    const double B = (Gright - Gp - A * std::cosh(kk * L)) / std::sinh(kk * L);
    Json p1Profile = Json::array();
    double maxErrP1 = 0.0;
    for (int c = 0; c < N; c += 10) {
        const double y = mesh.cellCentre[static_cast<std::size_t>(c)].y;
        const double Gex = Gp + A * std::cosh(kk * y) + B * std::sinh(kk * y);
        const double err = std::abs(p1[static_cast<std::size_t>(c)] - Gex) / std::abs(Gex);
        maxErrP1 = std::max(maxErrP1, err);
        p1Profile.push(Json::object().set("y", y).set("G", p1[static_cast<std::size_t>(c)]).set("GAnalytic", Gex));
    }

    j.set("dom", Json::object()
        .set("profile", domProfile)
        .set("maxRelErrorPlus", maxErrPlus)
        .set("maxRelErrorMinus", maxErrMinus)
        .set("netFluxLeft", dom.netFluxLeft)
        .set("netFluxRight", dom.netFluxRight));
    j.set("p1", Json::object()
        .set("profile", p1Profile)
        .set("maxRelError", maxErrP1));

    makeCheck("fvDOM directional intensity (+x)", maxErrPlus, 0.0, "-", 5.0e-3, "vs exact 1-D transport");
    makeCheck("fvDOM directional intensity (-x)", maxErrMinus, 0.0, "-", 5.0e-3, "vs exact 1-D transport");
    makeCheck("P1 diffusion-limit solution", maxErrP1, 0.0, "-", 5.0e-3, "Marshak boundaries");
    return j;
}

} // namespace

// =====================================================================================
//  main
// =====================================================================================
int main(int argc, char** argv) {
    const std::string outPath = (argc > 1) ? argv[1] : "verification.json";

    Json report = Json::object();
    Json meta = Json::object();
    meta.set("generated", std::string(__DATE__) + " " + __TIME__);
    meta.set("compiler", std::string("g++ -std=c++20 -O2"));
    meta.set("standard", std::string("C++20"));
    meta.set("engine", std::string("4-Quadrant Hilbert-sorted unstructured polyhedral FVM "
                                   "(FaceBaseSoA, compact CorrectionSoA, CellFaceCSR, Rhie-Chow SIMPLE/PISO)"));
    report.set("meta", meta);

    std::cout << "[1/5] composite CHT benchmarks ..." << std::endl;
    report.set("cht", benchmarkCHT());
    std::cout << "[2/5] All-y+ wall treatment ..." << std::endl;
    report.set("wallTreatment", benchmarkWallTreatment());
    std::cout << "[3/5] turbulent channel (SST + All-y+, WALE LES) ..." << std::endl;
    report.set("channel", benchmarkChannel());
    std::cout << "[4/5] anisotropic porous media ..." << std::endl;
    report.set("porous", benchmarkPorous());
    std::cout << "[5/5] thermal radiation (P1 + fvDOM) ..." << std::endl;
    report.set("radiation", benchmarkRadiation());

    // ---- summary table --------------------------------------------------------------
    Json checks = Json::array();
    int passed = 0;
    for (const auto& c : g_checks) {
        checks.push(checkJson(c));
        if (c.pass) ++passed;
    }
    report.set("checks", checks);
    report.set("summary", Json::object()
        .set("total", static_cast<int>(g_checks.size()))
        .set("passed", passed)
        .set("failed", static_cast<int>(g_checks.size()) - passed));

    std::ofstream f(outPath);
    if (f) { f << report.dump(0) << "\n"; }
    else std::cerr << "warning : cannot write " << outPath << std::endl;

    std::cout << "\n================ VERIFICATION SUMMARY ================\n";
    for (const auto& c : g_checks) {
        std::printf("%-46s %14.6g %-10s (target %.6g)  %s\n", c.name.c_str(), c.value, c.unit.c_str(),
                    c.target, c.pass ? "PASS" : "CHECK");
    }
    std::printf("passed %d / %d\n", passed, static_cast<int>(g_checks.size()));
    return 0;
}
