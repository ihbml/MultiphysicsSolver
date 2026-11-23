// =====================================================================================
//  mt::Solver.cpp -- segregated SIMPLE / PISO solver with Rhie-Chow interpolation.
// =====================================================================================
#include "mt/Solver.hpp"

#include "mt/Sparse.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mt {

// -------------------------------------------------------------------------------------
//  Construction
// -------------------------------------------------------------------------------------
namespace {
//! the turbulence model enum of the solver configuration is authoritative
TurbulenceConfig syncedTurbulence(const SolverConfig& c) {
    TurbulenceConfig t = c.turb;
    t.model = c.turbulence;
    return t;
}
} // namespace

Solver::Solver(Mesh& mesh, const SolverConfig& cfg)
    : mesh_(mesh), cfg_(cfg), turb_(mesh, syncedTurbulence(cfg)), wall_(cfg.wall) {
    cfg_.turb.model = cfg_.turbulence;
    const int n = mesh.nCells;
    p.assign(static_cast<std::size_t>(n), 0.0);
    T.assign(static_cast<std::size_t>(n), 300.0);
    k.assign(static_cast<std::size_t>(n), cfg.turb.kInf);
    omega.assign(static_cast<std::size_t>(n), cfg.turb.omegaInf);
    nuTilde.assign(static_cast<std::size_t>(n), 0.0);
    U.assign(static_cast<std::size_t>(n), Vec3{0.0, 0.0, 0.0});
    Uold_.assign(static_cast<std::size_t>(n), Vec3{0.0, 0.0, 0.0});
    Unew_.assign(static_cast<std::size_t>(n), Vec3{0.0, 0.0, 0.0});
    massFlux.assign(static_cast<std::size_t>(mesh.nFaces()), 0.0);
    wallShear.assign(static_cast<std::size_t>(mesh.nFaces()), 0.0);
    wallHeatFlux.assign(static_cast<std::size_t>(mesh.nFaces()), 0.0);
    wallUtau.assign(static_cast<std::size_t>(mesh.nFaces()), 0.0);
    gradUTensor_.assign(static_cast<std::size_t>(n), Mat3::identity());
    divU_.assign(static_cast<std::size_t>(n), 0.0);
    gradP_.assign(static_cast<std::size_t>(n), Vec3{0.0, 0.0, 0.0});
    gradT_.assign(static_cast<std::size_t>(n), Vec3{0.0, 0.0, 0.0});
    gradK_.assign(static_cast<std::size_t>(n), Vec3{0.0, 0.0, 0.0});
    gradW_.assign(static_cast<std::size_t>(n), Vec3{0.0, 0.0, 0.0});
    nut_.assign(static_cast<std::size_t>(n), 0.0);
    pNew_.assign(static_cast<std::size_t>(n), 0.0);
    Tnew_.assign(static_cast<std::size_t>(n), 0.0);
    kNew_.assign(static_cast<std::size_t>(n), 0.0);
    omegaNew_.assign(static_cast<std::size_t>(n), 0.0);
    interfaceTemperature.assign(static_cast<std::size_t>(cfg.cht.size()), 300.0);

    regionConductivity_.assign(static_cast<std::size_t>(n), 1.0);
    cfg_.porous.setCellCount(n);
    const int nPairs = cfg.cht.size();
    interfaceTemperature_.assign(static_cast<std::size_t>(nPairs), 300.0);
    interfaceConductance_.assign(static_cast<std::size_t>(nPairs), 0.0);
    for (int c = 0; c < n; ++c) {
        const int r = mesh.cellRegion[static_cast<std::size_t>(c)];
        if (r >= 0 && r < static_cast<int>(cfg.regions.size()))
            regionConductivity_[static_cast<std::size_t>(c)] = cfg.regions[static_cast<std::size_t>(r)].conductivity;
    }

    // cells that own a wall face (the All-y+ treatment acts on those)
    wallCell_.assign(static_cast<std::size_t>(n), 0);
    for (int f = 0; f < mesh.nFaces(); ++f) {
        const int bc = mesh.faces.bcId[static_cast<std::size_t>(f)];
        if (bc < 0) continue;
        if (mesh.bcs[static_cast<std::size_t>(bc)].type != BCType::Wall) continue;
        wallCell_[static_cast<std::size_t>(mesh.faces.owner[static_cast<std::size_t>(f)])] = 1;
    }
}

void Solver::initialise() {
    computeGradients();
    updateWallTreatment();
    turb_.update(gradUTensor_, divU_, k, omega, nuTilde, &gradK_, &gradW_);
    nut_ = turb_.nut();
    updateMassFluxes();
}

// -------------------------------------------------------------------------------------
//  Field gradients
// -------------------------------------------------------------------------------------
void Solver::computeGradients() {
    const int n = mesh_.nCells;
    std::vector<double> ux(n), uy(n), uz(n);
    for (int c = 0; c < n; ++c) {
        ux[static_cast<std::size_t>(c)] = U[static_cast<std::size_t>(c)].x;
        uy[static_cast<std::size_t>(c)] = U[static_cast<std::size_t>(c)].y;
        uz[static_cast<std::size_t>(c)] = U[static_cast<std::size_t>(c)].z;
    }
    const std::vector<Vec3> gx = mesh_.cellGradient(ux);
    const std::vector<Vec3> gy = mesh_.cellGradient(uy);
    const std::vector<Vec3> gz = mesh_.cellGradient(uz);
    for (int c = 0; c < n; ++c) {
        Mat3& G = gradUTensor_[static_cast<std::size_t>(c)];
        G(0, 0) = gx[static_cast<std::size_t>(c)].x; G(0, 1) = gx[static_cast<std::size_t>(c)].y; G(0, 2) = gx[static_cast<std::size_t>(c)].z;
        G(1, 0) = gy[static_cast<std::size_t>(c)].x; G(1, 1) = gy[static_cast<std::size_t>(c)].y; G(1, 2) = gy[static_cast<std::size_t>(c)].z;
        G(2, 0) = gz[static_cast<std::size_t>(c)].x; G(2, 1) = gz[static_cast<std::size_t>(c)].y; G(2, 2) = gz[static_cast<std::size_t>(c)].z;
        divU_[static_cast<std::size_t>(c)] = G(0, 0) + G(1, 1) + G(2, 2);
    }
    gradT_ = mesh_.cellGradient(T);
    gradK_ = mesh_.cellGradient(k);
    gradW_ = mesh_.cellGradient(omega);
}

// -------------------------------------------------------------------------------------
//  Wall treatment (All-y+)
// -------------------------------------------------------------------------------------
double Solver::wallNormalDistance(int face) const {
    //  y_P = | (x_P - x_f) . S_f | / ||S_f||   (exact wall normal distance)
    const Vec3& Sf = mesh_.faces.areaVector[static_cast<std::size_t>(face)];
    const Vec3& xf = mesh_.faces.faceCentre[static_cast<std::size_t>(face)];
    const Vec3& xp = mesh_.cellCentre[static_cast<std::size_t>(mesh_.faces.owner[static_cast<std::size_t>(face)])];
    const double a = mag(Sf);
    if (a < kSmall) return 0.0;
    return std::abs(dot(xp - xf, Sf)) / a;
}

int Solver::adjacentCell(int cell, int face) const {
    // the face may be owned by the cell or by its neighbour : return the other cell
    const int o = mesh_.faces.owner[static_cast<std::size_t>(face)];
    if (o == cell) return mesh_.faces.neighbour[static_cast<std::size_t>(face)];
    return o;
}

double Solver::wallTangentialSpeed(int face, const Vec3& u) const {
    const Vec3& n = mesh_.faces.unitNormal[static_cast<std::size_t>(face)];
    return mag(u - n * dot(u, n));
}

void Solver::updateWallTreatment() {
    const double nu = cfg_.mu / cfg_.rho;
    wallShearTotal_ = 0.0;
    for (int f = 0; f < mesh_.nFaces(); ++f) {
        const int bc = mesh_.faces.bcId[static_cast<std::size_t>(f)];
        if (bc < 0) continue;
        const BoundaryCondition& bcv = mesh_.bcs[static_cast<std::size_t>(bc)];
        if (bcv.type != BCType::Wall) continue;
        const int P = mesh_.faces.owner[static_cast<std::size_t>(f)];
        const double yP = wallNormalDistance(f);
        const double uPar = wallTangentialSpeed(f, U[static_cast<std::size_t>(P)]);
        const double uTau = wall_.frictionVelocity(yP, uPar, nu);
        wallUtau[static_cast<std::size_t>(f)] = uTau;
        wallShear[static_cast<std::size_t>(f)] = cfg_.rho * uTau * uTau;
        wallShearTotal_ += wallShear[static_cast<std::size_t>(f)] * mesh_.faces.area[static_cast<std::size_t>(f)];

        // ---- thermal wall condition (Kader profile) ------------------------------
        const double Tw = bcv.specifiedTemperature ? bcv.value : T[static_cast<std::size_t>(P)];
        if (bcv.specifiedHeatFlux) {
            wallHeatFlux[static_cast<std::size_t>(f)] = bcv.heatFlux;
        } else {
            const double q = wall_.wallHeatFlux(yP, T[static_cast<std::size_t>(P)], Tw, uTau, nu, cfg_.rho, cfg_.cp);
            wallHeatFlux[static_cast<std::size_t>(f)] = -q;   // positive = into the fluid
        }
    }
}

// -------------------------------------------------------------------------------------
//  Pressure gradient (SIMPLE interpolation or prescribed)
// -------------------------------------------------------------------------------------
void Solver::updatePressureGradient() {
    if (cfg_.prescribedPressureGradient) {
        for (auto& g : gradP_) g = cfg_.pressureGradient;
        return;
    }
    const int n = mesh_.nCells;
    for (int c = 0; c < n; ++c) {
        Vec3 g{0, 0, 0};
        const double V = mesh_.cellVolume[static_cast<std::size_t>(c)];
        const int e0 = mesh_.cellFaces.begin(c), e1 = mesh_.cellFaces.end(c);
        for (int e = e0; e < e1; ++e) {
            const int f = mesh_.cellFaces.faceIndex[static_cast<std::size_t>(e)];
            const int nb = adjacentCell(c, f);
            double pnb = p[static_cast<std::size_t>(c)];
            if (nb >= 0) {
                pnb = p[static_cast<std::size_t>(nb)];
            } else {
                const int bc = mesh_.faces.bcId[static_cast<std::size_t>(f)];
                if (bc >= 0 && mesh_.bcs[static_cast<std::size_t>(bc)].type == BCType::Outlet)
                    pnb = mesh_.bcs[static_cast<std::size_t>(bc)].pressure;
            }
            const double d = std::max(mesh_.faces.distance[static_cast<std::size_t>(f)], 1e-12);
            g += mesh_.faces.areaVector[static_cast<std::size_t>(f)] * ((pnb - p[static_cast<std::size_t>(c)]) / d);
        }
        gradP_[static_cast<std::size_t>(c)] = g / std::max(V, 1e-30);
    }
}

// -------------------------------------------------------------------------------------
//  Mass fluxes with Rhie-Chow interpolation
// -------------------------------------------------------------------------------------
void Solver::updateMassFluxes() {
    const int nf = mesh_.nFaces();
    for (int f = 0; f < nf; ++f) {
        const int P = mesh_.faces.owner[static_cast<std::size_t>(f)];
        const int N = mesh_.faces.neighbour[static_cast<std::size_t>(f)];
        const Vec3& n = mesh_.faces.unitNormal[static_cast<std::size_t>(f)];
        const double A = mesh_.faces.area[static_cast<std::size_t>(f)];
        if (N >= 0) {
            const double d = std::max(mesh_.faces.distance[static_cast<std::size_t>(f)], 1e-12);
            const double pPart = cfg_.rho * A * (p[static_cast<std::size_t>(P)] - p[static_cast<std::size_t>(N)]) / d;
            double rc = 0.0;
            if (cfg_.rhieChow) {
                const Vec3& xf = mesh_.faces.faceCentre[static_cast<std::size_t>(f)];
                const double dPf = mag(xf - mesh_.cellCentre[static_cast<std::size_t>(P)]);
                const double dNf = mag(mesh_.cellCentre[static_cast<std::size_t>(N)] - xf);
                const double sum = std::max(dPf + dNf, 1e-12);
                const Vec3 uFace = (dNf / sum) * U[static_cast<std::size_t>(P)] + (dPf / sum) * U[static_cast<std::size_t>(N)];
                rc = cfg_.rho * A * (dot(uFace, n) - dot(U[static_cast<std::size_t>(P)], n));
            }
            massFlux[static_cast<std::size_t>(f)] = pPart + rc;
        } else {
            const int bc = mesh_.faces.bcId[static_cast<std::size_t>(f)];
            if (bc < 0) { massFlux[static_cast<std::size_t>(f)] = 0.0; continue; }
            const BoundaryCondition& bcv = mesh_.bcs[static_cast<std::size_t>(bc)];
            if (bcv.type == BCType::Inlet) {
                massFlux[static_cast<std::size_t>(f)] = cfg_.rho * dot(bcv.velocity, n) * A;
            } else if (bcv.type == BCType::Outlet) {
                const double d = std::max(mesh_.faces.distance[static_cast<std::size_t>(f)], 1e-12);
                massFlux[static_cast<std::size_t>(f)] = cfg_.rho * A * (p[static_cast<std::size_t>(P)] - bcv.pressure) / d;
            } else {
                massFlux[static_cast<std::size_t>(f)] = 0.0;
            }
        }
    }
}

// -------------------------------------------------------------------------------------
//  Partitioned CHT interfaces (Robin-Robin)
// -------------------------------------------------------------------------------------
void Solver::computeCHTInterfaces(std::vector<double>& interfaceT, std::vector<double>& conductance) const {
    if (cfg_.regions.size() < 2) return;
    std::size_t idx = 0;
    for (const auto& pr : cfg_.cht.conformal()) {
        const std::vector<int> sf{pr.second};
        const std::vector<double> ar{mesh_.faces.area[static_cast<std::size_t>(pr.second)]};
        const CHTInterfaceState st = cfg_.cht.evaluate(mesh_, pr.first, sf, ar, T, gradT_,
                                                        cfg_.regions[0], cfg_.regions[1]);
        if (idx < interfaceT.size()) {
            interfaceT[idx] = st.interfaceTemperature;
            conductance[idx] = st.conductance * mesh_.faces.area[static_cast<std::size_t>(pr.first)];
        }
        ++idx;
    }
    for (const auto& nc : cfg_.cht.nonConformal()) {
        const CHTInterfaceState st = cfg_.cht.evaluate(mesh_, nc.fluidFace, nc.solidFaces, nc.overlapAreas,
                                                        T, gradT_, cfg_.regions[0], cfg_.regions[1]);
        if (idx < interfaceT.size()) {
            interfaceT[idx] = st.interfaceTemperature;
            conductance[idx] = st.conductance * mesh_.faces.area[static_cast<std::size_t>(nc.fluidFace)];
        }
        ++idx;
    }
}

void Solver::relaxCHTInterfaces() {
    if (cfg_.cht.size() == 0 || cfg_.chtMonolithic) return;
    std::vector<double> newT = interfaceTemperature_;
    std::vector<double> cond = interfaceConductance_;
    computeCHTInterfaces(newT, cond);
    const double lambda = clampd(cfg_.chtRelaxation, 0.0, 1.0);
    for (std::size_t i = 0; i < interfaceTemperature_.size(); ++i)
        interfaceTemperature_[i] = (1.0 - lambda) * interfaceTemperature_[i] + lambda * newT[i];
    interfaceConductance_ = cond;
}

// -------------------------------------------------------------------------------------
//  Momentum equation
// -------------------------------------------------------------------------------------
void Solver::assembleMomentum(double dt) {
    const int n = mesh_.nCells;
    const double rho = cfg_.rho;
    for (int comp = 0; comp < 3; ++comp) {
        CsrMatrix A;
        A.resize(n, n);
        std::vector<double> b(static_cast<std::size_t>(n), 0.0);
        for (int c = 0; c < n; ++c) {
            const double V = mesh_.cellVolume[static_cast<std::size_t>(c)];
            const double muEff = cfg_.mu + rho * nut_[static_cast<std::size_t>(c)];
            double diag = rho * V / dt;
            double rhs = rho * V / dt * Uold_[static_cast<std::size_t>(c)][comp];

            // ---- pressure gradient source :  - V grad p --------------------------
            rhs += -V * gradP_[static_cast<std::size_t>(c)][comp];

            // ---- anisotropic Darcy-Forchheimer porous sink ------------------------
            double porousDiag = 0.0;
            if (cfg_.porous.isPorous(c)) {
                const Vec3 sink = cfg_.porous.momentumSink(c, U[static_cast<std::size_t>(c)], rho, cfg_.mu);
                rhs += V * sink[comp];
                const Mat3 Sm = cfg_.porous.implicitSinkMatrix(c, U[static_cast<std::size_t>(c)], rho, cfg_.mu);
                porousDiag = Sm(comp, comp);
            }

            const int e0 = mesh_.cellFaces.begin(c), e1 = mesh_.cellFaces.end(c);
            for (int e = e0; e < e1; ++e) {
                const int f = mesh_.cellFaces.faceIndex[static_cast<std::size_t>(e)];
                const int nb = adjacentCell(c, f);
                const double Af = mesh_.faces.area[static_cast<std::size_t>(f)];
                const double F = massFlux[static_cast<std::size_t>(f)];
                if (nb >= 0) {
                    // ---- convection (first order upwind) -------------------------
                    if (F > 0.0) diag += rho * F;
                    else A.addEntry(c, nb, rho * F);
                    // ---- diffusion -------------------------------------------------
                    const double d = std::max(mesh_.faces.distance[static_cast<std::size_t>(f)], 1e-12);
                    const double af = muEff * Af / d;
                    diag += af;
                    A.addEntry(c, nb, -af);
                } else {
                    const int bc = mesh_.faces.bcId[static_cast<std::size_t>(f)];
                    if (bc < 0) continue;
                    const BoundaryCondition& bcv = mesh_.bcs[static_cast<std::size_t>(bc)];
                    if (bcv.type == BCType::Wall) {
                        const double dP = std::max(wallNormalDistance(f), 1e-12);
                        double muWall = muEff;
                        if (cfg_.wallFunctions) {
                            const double uPar = std::max(wallTangentialSpeed(f, U[static_cast<std::size_t>(c)]), 1e-6);
                            muWall = wallShear[static_cast<std::size_t>(f)] * dP / uPar;
                        }
                        diag += muWall * Af / dP;
                    } else if (bcv.type == BCType::Inlet) {
                        rhs -= F * bcv.velocity[comp];
                    } else if (bcv.type == BCType::Outlet) {
                        diag += rho * F;
                    }
                }
            }
            diag += porousDiag * V;
            A.addEntry(c, c, diag);
            b[static_cast<std::size_t>(c)] = rhs;
        }
        A.compress();
        std::vector<double> x;
        biCGStab(A, b, x, 1.0e-12, 3000, true);
        for (int c = 0; c < n; ++c) Unew_[static_cast<std::size_t>(c)][comp] = x[static_cast<std::size_t>(c)];
    }
}

// -------------------------------------------------------------------------------------
//  Pressure equation (SIMPLE / PISO) with Rhie-Chow source
// -------------------------------------------------------------------------------------
void Solver::assemblePressure() {
    const int n = mesh_.nCells;
    const int nf = mesh_.nFaces();
    CsrMatrix A;
    A.resize(n, n);
    std::vector<double> b(static_cast<std::size_t>(n), 0.0);

    for (int f = 0; f < nf; ++f) {
        const int P = mesh_.faces.owner[static_cast<std::size_t>(f)];
        const int N = mesh_.faces.neighbour[static_cast<std::size_t>(f)];
        const double Af = mesh_.faces.area[static_cast<std::size_t>(f)];
        if (N >= 0) {
            const double d = std::max(mesh_.faces.distance[static_cast<std::size_t>(f)], 1e-12);
            const double af = cfg_.rho * Af / d;
            A.addEntry(P, P, af);
            A.addEntry(N, N, af);
            A.addEntry(P, N, -af);
            A.addEntry(N, P, -af);
            double rc = 0.0;
            if (cfg_.rhieChow) {
                const Vec3& n = mesh_.faces.unitNormal[static_cast<std::size_t>(f)];
                const Vec3& xf = mesh_.faces.faceCentre[static_cast<std::size_t>(f)];
                const double dPf = mag(xf - mesh_.cellCentre[static_cast<std::size_t>(P)]);
                const double dNf = mag(mesh_.cellCentre[static_cast<std::size_t>(N)] - xf);
                const double sum = std::max(dPf + dNf, 1e-12);
                const Vec3 uFace = (dNf / sum) * U[static_cast<std::size_t>(P)] + (dPf / sum) * U[static_cast<std::size_t>(N)];
                rc = cfg_.rho * Af * (dot(uFace, n) - dot(U[static_cast<std::size_t>(P)], n));
            }
            b[static_cast<std::size_t>(P)] -= rc;
            b[static_cast<std::size_t>(N)] += rc;
        } else {
            const int bc = mesh_.faces.bcId[static_cast<std::size_t>(f)];
            if (bc < 0) continue;
            const BoundaryCondition& bcv = mesh_.bcs[static_cast<std::size_t>(bc)];
            if (bcv.type == BCType::Outlet) {
                const double d = std::max(mesh_.faces.distance[static_cast<std::size_t>(f)], 1e-12);
                const double af = cfg_.rho * Af / d;
                A.addEntry(P, P, af);
                b[static_cast<std::size_t>(P)] += af * bcv.pressure;
            } else if (bcv.type == BCType::Inlet) {
                b[static_cast<std::size_t>(P)] -= massFlux[static_cast<std::size_t>(f)];
            }
        }
    }
    A.compress();
    std::vector<double> x;
    biCGStab(A, b, x, 1.0e-12, 5000, true);
    for (int c = 0; c < n; ++c) pNew_[static_cast<std::size_t>(c)] = x[static_cast<std::size_t>(c)];
}

// -------------------------------------------------------------------------------------
//  Energy equation (with multi-region CHT coupling)
// -------------------------------------------------------------------------------------
void Solver::assembleEnergy(double dt) {
    const int n = mesh_.nCells;
    const double rho = cfg_.rho, cp = cfg_.cp;

    // ---- monolithic cross-region coupling terms ----------------------------------
    std::vector<CHTCouplingTerm> chtTerms;
    if (cfg_.chtMonolithic && cfg_.cht.size() > 0 && cfg_.regions.size() >= 2) {
        chtTerms = cfg_.cht.monolithicTerms(mesh_, T, gradT_, cfg_.regions, 0, 1);
    }

    CsrMatrix A;
    A.resize(n, n);
    std::vector<double> b(static_cast<std::size_t>(n), 0.0);

    for (int c = 0; c < n; ++c) {
        const double V = mesh_.cellVolume[static_cast<std::size_t>(c)];
        const double kP = regionConductivity_[static_cast<std::size_t>(c)];
        const double lambdaEff = kP + rho * cp * nut_[static_cast<std::size_t>(c)] / cfg_.Prt;
        double diag = rho * cp * V / dt;
        double rhs = rho * cp * V / dt * T[static_cast<std::size_t>(c)];

        const int e0 = mesh_.cellFaces.begin(c), e1 = mesh_.cellFaces.end(c);
        for (int e = e0; e < e1; ++e) {
            const int f = mesh_.cellFaces.faceIndex[static_cast<std::size_t>(e)];
            const int nb = adjacentCell(c, f);
            const double Af = mesh_.faces.area[static_cast<std::size_t>(f)];
            const double F = massFlux[static_cast<std::size_t>(f)];
            if (nb >= 0) {
                if (F > 0.0) diag += cp * F;
                else A.addEntry(c, nb, cp * F);
                const double d = std::max(mesh_.faces.distance[static_cast<std::size_t>(f)], 1e-12);
                const double kN = regionConductivity_[static_cast<std::size_t>(nb)];
                const double lamF = 2.0 * lambdaEff * kN / std::max(lambdaEff + kN, 1e-30);
                const double af = lamF * Af / d;
                diag += af;
                A.addEntry(c, nb, -af);
            } else {
                const int bc = mesh_.faces.bcId[static_cast<std::size_t>(f)];
                if (bc < 0) continue;
                const BoundaryCondition& bcv = mesh_.bcs[static_cast<std::size_t>(bc)];
                if (bcv.type == BCType::Wall) {
                    const double dP = std::max(wallNormalDistance(f), 1e-12);
                    if (bcv.specifiedTemperature) {
                        const double af = lambdaEff * Af / dP;
                        diag += af;
                        rhs += af * bcv.value;
                    } else if (bcv.specifiedConvective) {
                        // Robin condition : q = h (T_inf - T_P)
                        const double ah = bcv.heatTransferCoefficient * Af;
                        diag += ah;
                        rhs += ah * bcv.ambientTemperature;
                    } else {
                        rhs += wallHeatFlux[static_cast<std::size_t>(f)] * Af;
                    }
                } else if (bcv.type == BCType::InterfaceCHT) {
                    if (!cfg_.chtMonolithic) {
                        // partitioned Robin-Robin condition (under-relaxed interface value)
                        const int pi = cfg_.cht.pairIndexOfFace(f);
                        if (pi >= 0 && pi < static_cast<int>(interfaceConductance_.size())) {
                            const double af = interfaceConductance_[static_cast<std::size_t>(pi)];
                            diag += af;
                            rhs += af * interfaceTemperature_[static_cast<std::size_t>(pi)];
                        }
                    }
                }
            }
        }
        A.addEntry(c, c, diag);
        b[static_cast<std::size_t>(c)] = rhs;
    }

    // ---- monolithic CHT cross-region coefficients ---------------------------------
    for (const auto& t : chtTerms) {
        const double ca = t.conductance * t.area;
        A.addEntry(t.fluidCell, t.fluidCell, ca);
        A.addEntry(t.fluidCell, t.solidCell, -ca);
        A.addEntry(t.solidCell, t.solidCell, ca);
        A.addEntry(t.solidCell, t.fluidCell, -ca);
        b[static_cast<std::size_t>(t.fluidCell)] -= t.skewSource;
        b[static_cast<std::size_t>(t.solidCell)] += t.skewSource;
    }
    A.compress();
    if (std::getenv("MT_DEBUG_MATRIX")) {
        for (int r = 0; r < n; ++r) {
            std::fprintf(stderr, "ENERGY ROW %d:", r);
            for (int e = A.rowPtr[r]; e < A.rowPtr[r+1]; ++e)
                std::fprintf(stderr, " (%d,%g)", A.colIdx[e], A.values[e]);
            std::fprintf(stderr, "  b=%g\n", b[r]);
        }
    }
    std::vector<double> x;
    biCGStab(A, b, x, 1.0e-12, 5000, true);
    if (std::getenv("MT_DEBUG_MATRIX"))
        for (int r = 0; r < n; ++r) std::fprintf(stderr, "  x[%d]=%g\n", r, x[r]);
    for (int c = 0; c < n; ++c) Tnew_[static_cast<std::size_t>(c)] = x[static_cast<std::size_t>(c)];
}

// -------------------------------------------------------------------------------------
//  Turbulence transport equations (Patankar positivity preserving)
// -------------------------------------------------------------------------------------
void Solver::assembleTurbulenceScalar(bool omegaEquation, double dt) {
    const int n = mesh_.nCells;
    const double rho = cfg_.rho;
    const double nu = cfg_.mu / rho;
    std::vector<double>& phi    = omegaEquation ? omega : k;
    std::vector<double>& phiNew = omegaEquation ? omegaNew_ : kNew_;
    const std::vector<double>& sigma = turb_.sigmaW();
    const std::vector<double>& betaC = turb_.beta();

    CsrMatrix A;
    A.resize(n, n);
    std::vector<double> b(static_cast<std::size_t>(n), 0.0);

    for (int c = 0; c < n; ++c) {
        const double V = mesh_.cellVolume[static_cast<std::size_t>(c)];
        const double sig = sigma.empty() ? 1.0 : sigma[static_cast<std::size_t>(c)];

        // ---- All-y+ wall treatment : prescribe the near-wall cell value ---------
        if (cfg_.wallFunctions && wallCell_[static_cast<std::size_t>(c)]) {
            int wf = -1;
            const int e0 = mesh_.cellFaces.begin(c), e1 = mesh_.cellFaces.end(c);
            for (int e = e0; e < e1; ++e) {
                const int f = mesh_.cellFaces.faceIndex[static_cast<std::size_t>(e)];
                const int bc = mesh_.faces.bcId[static_cast<std::size_t>(f)];
                if (bc >= 0 && mesh_.bcs[static_cast<std::size_t>(bc)].type == BCType::Wall) { wf = f; break; }
            }
            if (wf >= 0) {
                const double uTau = wallUtau[static_cast<std::size_t>(wf)];
                const double yP = wallNormalDistance(wf);
                double value = 0.0;
                if (omegaEquation) {
                    // omega_P = sqrt(omega_vis^2 + omega_log^2)
                    value = wall_.blendedOmega(yP, uTau, nu);
                } else {
                    // equilibrium near-wall value  k = u_tau^2 / sqrt(beta*)
                    value = uTau * uTau / std::sqrt(cfg_.turb.betaStar);
                }
                A.addEntry(c, c, 1.0);
                b[static_cast<std::size_t>(c)] = value;
                continue;
            }
        }

        const double diff = cfg_.mu + rho * nut_[static_cast<std::size_t>(c)] / std::max(sig, 1e-6);
        double diag = rho * V / dt;
        double rhs = rho * V / dt * phi[static_cast<std::size_t>(c)];

        // ---- Patankar linearisation of the source term ---------------------------
        //   k    :  S = P_k - beta* rho omega k   -> a_PP += beta* rho omega V, b_P += P_k V
        //   omega:  S = gamma rho P_k/nu_t - beta rho omega^2
        //                              -> a_PP += 2 beta rho omega V,
        //                                 b_P  += (gamma rho P_k/nu_t + beta rho omega^2) V
        if (omegaEquation) {
            const double nutCell = std::max(nut_[static_cast<std::size_t>(c)], 1e-12);
            const double Pk = turb_.production()[static_cast<std::size_t>(c)];
            const double gam = turb_.gamma()[static_cast<std::size_t>(c)];
            const double bet = betaC.empty() ? cfg_.turb.beta1 : betaC[static_cast<std::size_t>(c)];
            const double w = std::max(phi[static_cast<std::size_t>(c)], 1e-12);
            diag += 2.0 * bet * rho * w * V;
            rhs += (gam * rho * Pk / nutCell + bet * rho * w * w) * V;
            // ---- cross diffusion (Menter): 2 (1-F1) rho sigma_w2 (1/omega) grad k . grad w
            const double Dc = turb_.crossDiffusion()[static_cast<std::size_t>(c)];
            rhs += Dc * V;
            if (Dc < 0.0) diag += -Dc * V;
        } else {
            const double bet = cfg_.turb.betaStar;
            const double Pk = turb_.production()[static_cast<std::size_t>(c)];
            const double w = std::max(phi[static_cast<std::size_t>(c)], 1e-12);
            diag += bet * rho * w * V;
            rhs += Pk * V;
        }

        const int e0 = mesh_.cellFaces.begin(c), e1 = mesh_.cellFaces.end(c);
        for (int e = e0; e < e1; ++e) {
            const int f = mesh_.cellFaces.faceIndex[static_cast<std::size_t>(e)];
            const int nb = adjacentCell(c, f);
            const double Af = mesh_.faces.area[static_cast<std::size_t>(f)];
            const double F = massFlux[static_cast<std::size_t>(f)];
            if (nb >= 0) {
                if (F > 0.0) diag += rho * F;
                else A.addEntry(c, nb, rho * F);
                const double d = std::max(mesh_.faces.distance[static_cast<std::size_t>(f)], 1e-12);
                const double af = diff * Af / d;
                diag += af;
                A.addEntry(c, nb, -af);
            } else {
                const int bc = mesh_.faces.bcId[static_cast<std::size_t>(f)];
                if (bc < 0) continue;
                const BoundaryCondition& bcv = mesh_.bcs[static_cast<std::size_t>(bc)];
                if (bcv.type == BCType::Wall) {
                    if (cfg_.wallFunctions) continue;      // prescribed at the cell level
                    const double dP = std::max(wallNormalDistance(f), 1e-12);
                    const double af = diff * Af / dP;
                    diag += af;
                    if (omegaEquation) {
                        // resolved wall : omega_w = 6 nu / (beta1 y^2)
                        rhs += af * (6.0 * nu / (cfg_.turb.beta1 * dP * dP));
                    } else {
                        rhs += af * 0.0;                      // k = 0 at the wall
                    }
                }
            }
        }
        A.addEntry(c, c, diag);
        b[static_cast<std::size_t>(c)] = rhs;
    }
    A.compress();
    std::vector<double> x;
    biCGStab(A, b, x, 1.0e-12, 4000, true);
    for (int c = 0; c < n; ++c) {
        double v = x[static_cast<std::size_t>(c)];
        if (!(v > 0.0)) v = 1.0e-12;      // unconditional positivity safeguard
        phiNew[static_cast<std::size_t>(c)] = v;
    }
}

// -------------------------------------------------------------------------------------
//  Steady (pseudo transient SIMPLE) driver
// -------------------------------------------------------------------------------------
SolverDiagnostics Solver::runSteady() {
    const int n = mesh_.nCells;
    diag_ = SolverDiagnostics{};
    const double dt = cfg_.pseudoTimeStep;

    for (int it = 0; it < cfg_.maxIterations; ++it) {
        Uold_ = U;
        computeGradients();
        updateWallTreatment();
        turb_.update(gradUTensor_, divU_, k, omega, nuTilde, &gradK_, &gradW_);
        nut_ = turb_.nut();

        assembleMomentum(dt);
        double resU = 0.0;
        double uScale = 1e-12;
        for (int c = 0; c < n; ++c) uScale = std::max(uScale, mag(U[static_cast<std::size_t>(c)]));
        for (int c = 0; c < n; ++c) {
            const Vec3 relaxed = Uold_[static_cast<std::size_t>(c)]
                               + cfg_.underRelaxU * (Unew_[static_cast<std::size_t>(c)] - Uold_[static_cast<std::size_t>(c)]);
            resU = std::max(resU, mag(relaxed - U[static_cast<std::size_t>(c)]) / uScale);
            U[static_cast<std::size_t>(c)] = relaxed;
        }

        double resP = 0.0;
        if (cfg_.solvePressure && !cfg_.prescribedPressureGradient) {
            assemblePressure();
            double pScale = 1e-12;
            for (int c = 0; c < n; ++c) pScale = std::max(pScale, std::abs(p[static_cast<std::size_t>(c)]));
            for (int c = 0; c < n; ++c) {
                const double relaxed = p[static_cast<std::size_t>(c)]
                                     + cfg_.underRelaxP * (pNew_[static_cast<std::size_t>(c)] - p[static_cast<std::size_t>(c)]);
                resP = std::max(resP, std::abs(relaxed - p[static_cast<std::size_t>(c)]) / pScale);
                p[static_cast<std::size_t>(c)] = relaxed;
            }
            updateMassFluxes();
        } else {
            updatePressureGradient();
            updateMassFluxes();
        }

        double resK = 0.0, resW = 0.0;
        const bool hasTransport = (cfg_.turbulence == TurbModel::SST || cfg_.turbulence == TurbModel::SA);
        if (hasTransport) assembleTurbulenceScalar(false, dt);
        {
            double kScale = 1e-12;
            for (int c = 0; c < n; ++c) kScale = std::max(kScale, std::abs(k[static_cast<std::size_t>(c)]));
            for (int c = 0; c < n; ++c) {
                const double relaxed = k[static_cast<std::size_t>(c)]
                                     + cfg_.underRelaxK * (kNew_[static_cast<std::size_t>(c)] - k[static_cast<std::size_t>(c)]);
                resK = std::max(resK, std::abs(relaxed - k[static_cast<std::size_t>(c)]) / kScale);
                k[static_cast<std::size_t>(c)] = std::max(relaxed, 1.0e-12);
            }
        }
        if (hasTransport) assembleTurbulenceScalar(true, dt);
        {
            double wScale = 1e-12;
            for (int c = 0; c < n; ++c) wScale = std::max(wScale, std::abs(omega[static_cast<std::size_t>(c)]));
            for (int c = 0; c < n; ++c) {
                const double relaxed = omega[static_cast<std::size_t>(c)]
                                     + cfg_.underRelaxW * (omegaNew_[static_cast<std::size_t>(c)] - omega[static_cast<std::size_t>(c)]);
                resW = std::max(resW, std::abs(relaxed - omega[static_cast<std::size_t>(c)]) / wScale);
                omega[static_cast<std::size_t>(c)] = std::max(relaxed, 1.0e-12);
            }
        }

        double resT = 0.0;
        if (cfg_.solveEnergy) {
            if (!cfg_.chtMonolithic && cfg_.cht.size() > 0) {
                std::vector<double> cond = interfaceConductance_;
                computeCHTInterfaces(interfaceTemperature_, cond);
                interfaceConductance_ = cond;
            }
            assembleEnergy(dt);
            double tScale = 1e-12;
            for (int c = 0; c < n; ++c) tScale = std::max(tScale, std::abs(T[static_cast<std::size_t>(c)]));
            for (int c = 0; c < n; ++c) {
                const double relaxed = T[static_cast<std::size_t>(c)]
                                     + cfg_.underRelaxT * (Tnew_[static_cast<std::size_t>(c)] - T[static_cast<std::size_t>(c)]);
                resT = std::max(resT, std::abs(relaxed - T[static_cast<std::size_t>(c)]) / tScale);
                T[static_cast<std::size_t>(c)] = relaxed;
            }
            relaxCHTInterfaces();
        }

        diag_.iterations = it + 1;
        diag_.residualU = resU; diag_.residualP = resP;
        diag_.residualK = resK; diag_.residualOmega = resW; diag_.residualT = resT;
        diag_.historyU.push_back(resU);
        diag_.historyP.push_back(resP);
        diag_.historyT.push_back(resT);
        diag_.historyK.push_back(resK);
        diag_.historyOmega.push_back(resW);

        const auto d = turb_.diagnostics(k, omega);
        diag_.minK = d.minK; diag_.minOmega = d.minOmega; diag_.limiterCells = d.limiterCells;

        if (std::max(std::max(resU, resP), std::max(resK, std::max(resW, resT))) < cfg_.tolerance) {
            diag_.converged = true;
            break;
        }
    }
    return diag_;
}

// -------------------------------------------------------------------------------------
//  Transient (PISO) driver
// -------------------------------------------------------------------------------------
SolverDiagnostics Solver::advance() {
    const int n = mesh_.nCells;
    const double dt = cfg_.timeStep;
    diag_ = SolverDiagnostics{};

    Uold_ = U;
    computeGradients();
    updateWallTreatment();
    turb_.update(gradUTensor_, divU_, k, omega, nuTilde, &gradK_, &gradW_);
    nut_ = turb_.nut();

    assembleMomentum(dt);
    for (int c = 0; c < n; ++c)
        U[static_cast<std::size_t>(c)] = Uold_[static_cast<std::size_t>(c)]
                                      + cfg_.underRelaxU * (Unew_[static_cast<std::size_t>(c)] - Uold_[static_cast<std::size_t>(c)]);

    if (cfg_.solvePressure && !cfg_.prescribedPressureGradient) {
        for (int piso = 0; piso < std::max(1, cfg_.pisoIterations); ++piso) {
            updatePressureGradient();
            updateMassFluxes();
            assemblePressure();
            for (int c = 0; c < n; ++c)
                p[static_cast<std::size_t>(c)] += cfg_.underRelaxP * (pNew_[static_cast<std::size_t>(c)] - p[static_cast<std::size_t>(c)]);
        }
    }
    updateMassFluxes();

    const bool hasTransport = (cfg_.turbulence == TurbModel::SST || cfg_.turbulence == TurbModel::SA);
    if (hasTransport) assembleTurbulenceScalar(false, dt);
    for (int c = 0; c < n; ++c)
        k[static_cast<std::size_t>(c)] = std::max(k[static_cast<std::size_t>(c)]
                                       + cfg_.underRelaxK * (kNew_[static_cast<std::size_t>(c)] - k[static_cast<std::size_t>(c)]), 1.0e-12);
    if (hasTransport) assembleTurbulenceScalar(true, dt);
    for (int c = 0; c < n; ++c)
        omega[static_cast<std::size_t>(c)] = std::max(omega[static_cast<std::size_t>(c)]
                                       + cfg_.underRelaxW * (omegaNew_[static_cast<std::size_t>(c)] - omega[static_cast<std::size_t>(c)]), 1.0e-12);

    if (cfg_.solveEnergy) {
        if (!cfg_.chtMonolithic && cfg_.cht.size() > 0) {
            std::vector<double> cond = interfaceConductance_;
            computeCHTInterfaces(interfaceTemperature_, cond);
            interfaceConductance_ = cond;
        }
        assembleEnergy(dt);
        for (int c = 0; c < n; ++c)
            T[static_cast<std::size_t>(c)] += cfg_.underRelaxT * (Tnew_[static_cast<std::size_t>(c)] - T[static_cast<std::size_t>(c)]);
        relaxCHTInterfaces();
    }

    const auto d = turb_.diagnostics(k, omega);
    diag_.minK = d.minK; diag_.minOmega = d.minOmega; diag_.limiterCells = d.limiterCells;
    diag_.iterations = 1;
    return diag_;
}

} // namespace mt
